// A1, A3 (namespace-s6-cutover): the authority's W5 session endpoint answers W6 from S5.2's history
// over TLS. A single voter enables positions on the first session that needs them, then takes
// writes, seals, rolls up and cuts under a small policy whose every seal writes the `layout`
// table. For every store cursor of a namespace (none, another incarnation, each R from 0 to h + 1)
// the HTTP body, defragmented, is byte for byte what an oracle built from the voter's own table,
// files and the producer's writes encodes; the tier follows the cut. Grants are W13 patterns:
// `d.*` reaches `d.x`, an exact grant does not, `*` follows the directory with `all`. Session
// errors keep version 1's statuses.
#include "detent_distribution/framing.hh"
#include "detent_distribution/session_frames.hh"
#include "history.hh"
#include "service.hh"
#include "consensus/transport/tls.h"
#include <filesystem>
#include <fstream>
#include <http_asio_tls.h>
#include <iostream>
#include <rapidjson/document.h>
#include <source_location>
#include <unistd.h>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace h = detent::authority::history;
using namespace std::chrono_literals;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("frames service invariant at " + std::to_string(where.line()));
}
std::string hex(std::span<const char> bytes) {
	std::string out;
	for (unsigned char byte : bytes) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
	return out;
}
struct Response {
	unsigned status;
	std::map<std::string, std::string> headers;
	std::string body;
};
Response request(asio::ip::tcp::endpoint endpoint, const std::filesystem::path &cert, std::string_view path,
				 const std::string &body, std::string_view token) {
	asio::io_context io;
	asio::ssl::context tls(asio::ssl::context::tls);
	reactor::tls::configure(tls, {"", "", (cert / "ca.pem").string()}, reactor::tls::Role::Client);
	asio::ssl::stream<asio::ip::tcp::socket> socket(io, tls);
	reactor::tls::bind_hostname(socket.native_handle(), "localhost");
	socket.next_layer().connect(endpoint);
	socket.handshake(asio::ssl::stream_base::client);
	const auto wire = "POST " + std::string(path) + " HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer " +
					  std::string(token) + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
					  std::to_string(body.size()) + "\r\n\r\n" + body;
	asio::write(socket, asio::buffer(wire));
	std::string received;
	std::array<char, 4096> scratch;
	asio::error_code error;
	while (!error) {
		auto n = socket.read_some(asio::buffer(scratch), error);
		received.append(scratch.data(), n);
	}
	const auto split = received.find("\r\n\r\n");
	check(split != std::string::npos);
	Response response{static_cast<unsigned>(std::stoul(received.substr(9, 3))), {}, {}};
	auto at = received.find("\r\n") + 2;
	while (at < split) {
		const auto end = received.find("\r\n", at), colon = received.find(':', at);
		response.headers.emplace(received.substr(at, colon - at), received.substr(colon + 2, end - colon - 2));
		at = end + 2;
	}
	if (response.headers.contains("Transfer-Encoding")) {
		at = split + 4;
		for (;;) {
			const auto end = received.find("\r\n", at);
			const auto n = std::stoull(received.substr(at, end - at), nullptr, 16);
			at = end + 2;
			response.body.append(received, at, n);
			at += n + 2;
			if (!n)
				break;
		}
	} else
		response.body = received.substr(split + 4);
	return response;
}
// The W6 body of a 200 next: the fragments' payloads, in order.
std::string frames_body(const Response &response, const std::string &session, bool compressed) {
	if (response.status != 200)
		std::cerr << "frames status " << response.status << ": " << response.body << '\n';
	check(response.status == 200 && response.headers.at("Content-Type") == "application/vnd.detent.frames" &&
		  response.headers.at("X-Detent-Session-Version") == "2" && response.headers.at("X-Detent-Session") == session &&
		  response.headers.at("X-Detent-Coding") == (compressed ? "zstd" : "raw") &&
		  !response.headers.contains("X-Detent-Kind") && !response.headers.contains("X-Detent-Revision"));
	d::FragmentDecoder decoder(d::UpdateKind::Frames, 256 * 1024 * 1024, compressed);
	auto input = std::span<const char>(response.body.data(), response.body.size());
	std::string bytes;
	while (!input.empty()) {
		d::WorkBudget work{4096, 2};
		decoder.feed(input, work);
		if (auto fragment = decoder.take())
			bytes.append(fragment->bytes());
	}
	decoder.finish();
	return bytes;
}
struct Seen final : d::FrameSink {
	std::vector<std::pair<std::string, d::NamespaceCursor>> frames;
	std::vector<std::tuple<d::PieceKind, std::uint64_t, std::uint64_t>> pieces;
	std::optional<d::DirectoryUpdate> update;
	void directory(const d::DirectoryUpdate &value) override { update = value; }
	void frame(std::string_view name, d::FrameKind, std::uint64_t, const d::NamespaceCursor &to) override {
		frames.emplace_back(std::string(name), to);
	}
	void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override {
		pieces.emplace_back(kind, first, last);
	}
	void record(std::string_view, std::uint64_t, const detent::KnobDef *) override {}
};
Seen decode(const std::string &bytes) {
	Seen seen;
	d::SessionFramesDecoder decoder(seen);
	std::span<const char> input(bytes.data(), bytes.size());
	std::size_t budget = bytes.size();
	decoder.feed(input, budget);
	decoder.finish();
	return seen;
}
// What the producer wrote at each position after the enabling entry.
using Entry = std::vector<std::pair<std::string, std::optional<detent::KnobDef>>>;
// The oracle: the voter's table and files on disk for the base, blocks and level-0 sections, the
// producer's writes for the ring.
class Rows final : public d::HistoryReader {
  public:
	explicit Rows(std::vector<std::tuple<std::string, std::uint64_t, std::optional<detent::KnobDef>>> rows)
		: rows_(std::move(rows)) {}
	const d::HistoryRecord *next() override {
		if (at_ == rows_.size())
			return nullptr;
		const auto &[key, revision, value] = rows_[at_++];
		record_ = {key, revision, value ? &*value : nullptr};
		return &record_;
	}

  private:
	std::vector<std::tuple<std::string, std::uint64_t, std::optional<detent::KnobDef>>> rows_;
	std::size_t at_ = 0;
	d::HistoryRecord record_;
};
class Oracle final : public d::NamespaceHistory {
  public:
	Oracle(d::Identity cluster, h::TableNamespace n, h::Files &files, std::map<std::uint64_t, Entry> ring,
		   std::uint64_t head)
		: cluster_(cluster), n_(std::move(n)), files_(files), ring_(std::move(ring)), head_(head) {
		for (const auto *list : {&n_.blocks, &n_.level0})
			for (const auto &block : *list) {
				tiers_.push_back(&block);
				spans_.push_back({block.first, block.last, list == &n_.blocks});
			}
	}
	d::NamespaceCursor head() const noexcept override { return {cluster_, n_.incarnation, head_}; }
	std::uint64_t cut() const noexcept override { return n_.base.last; }
	std::uint64_t ring_start() const noexcept override { return n_.position; }
	std::span<const d::HistorySpan> spans() const noexcept override { return spans_; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t i) const override {
		return rows(h::read_tier(n_, *tiers_.at(i), files_, {}));
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
		std::vector<std::tuple<std::string, std::uint64_t, std::optional<detent::KnobDef>>> out;
		for (auto it = ring_.upper_bound(after); it != ring_.end() && it->first <= head_; ++it) {
			auto entry = it->second;
			std::sort(entry.begin(), entry.end(), [](auto &x, auto &y) { return x.first < y.first; });
			for (auto &[key, value] : entry)
				out.emplace_back(key, it->first, value);
		}
		return std::make_unique<Rows>(std::move(out));
	}
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		auto copy = h::read_tier(n_, n_.base, files_, {});
		for (const auto *tier : tiers_)
			copy = h::merge(copy, h::read_tier(n_, *tier, files_, {}), true);
		return rows(std::move(copy));
	}

  private:
	static std::unique_ptr<d::HistoryReader> rows(h::Changes changes) {
		std::vector<std::tuple<std::string, std::uint64_t, std::optional<detent::KnobDef>>> out;
		for (auto &c : changes)
			out.emplace_back(c.key, c.revision,
							 c.definition ? std::optional(a::decode_definition(*c.definition)) : std::nullopt);
		return std::make_unique<Rows>(std::move(out));
	}
	d::Identity cluster_;
	h::TableNamespace n_;
	h::Files &files_;
	std::map<std::uint64_t, Entry> ring_;
	std::uint64_t head_;
	std::vector<const h::TableBlock *> tiers_;
	std::vector<d::HistorySpan> spans_;
};
struct DiskFiles final : h::Files {
	std::filesystem::path root;
	std::string read(std::string_view name) override {
		std::ifstream in(root / std::string(name), std::ios::binary);
		check(static_cast<bool>(in));
		return {std::istreambuf_iterator<char>(in), {}};
	}
};
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc == 2);
		std::filesystem::path cert(argv[1]);
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("frames-service-" + std::to_string(getpid()));
		std::filesystem::create_directories(directory.parent_path());
		a::ServiceOptions options;
		options.directory = directory;
		options.requests = 8;
		options.node.context.cluster[0] = 'c';
		options.node.context.group[0] = 'g';
		options.node.context.epoch[0] = 'e';
		options.node.configuration.cluster = options.node.context.cluster;
		options.node.configuration.configuration = options.node.context.group;
		options.node.configuration.local = 1;
		options.node.configuration.voters = {1};
		auto &policy = options.node.application.layout;
		policy.seal_transactions = 3;
		policy.period = 1; // Every seal writes the table the oracle reads.
		policy.carry_count = 0;
		policy.alpha_num = 1;
		policy.alpha_den = 2;
		const std::string writer = "0123456789abcdef0123456789abcdef", reader = "fedcba9876543210fedcba9876543210",
						  watcher = "00112233445566778899aabbccddeeff";
		options.credentials.push_back({writer, "writer", {"ns", "d.*"}});
		options.credentials.push_back({reader, "reader", {"ns"}, {}, 1, false});
		options.credentials.push_back({watcher, "watcher", {"*"}, {}, 1, false});
		const auto context = options.node.context;
		const auto identity = "\"cluster\":\"" + hex(context.cluster) + "\",\"group\":\"" + hex(context.group) +
							  "\",\"epoch\":\"" + hex(context.epoch) + "\",";
		a::Service authority(options);
		auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(
			*tls, {(cert / "one.pem").string(), (cert / "one.key").string(), (cert / "ca.pem").string()},
			reactor::tls::Role::Server);
		http::HttpAsioService server(authority, 1, 4, http::tls_connection_factory(tls, 2s), 4);
		http::AsioBindOptions bind;
		bind.address = "127.0.0.1";
		server.set_bind_options(bind);
		server.start(0);
		const asio::ip::tcp::endpoint endpoint{asio::ip::address_v4::loopback(), server.port()};
		auto call = [&](std::string_view path, const std::string &fields) {
			return request(endpoint, cert, path, "{" + identity + fields + "}", writer);
		};
		Response open;
		for (auto deadline = std::chrono::steady_clock::now() + 5s; std::chrono::steady_clock::now() < deadline;) {
			open = call("/v2/open", "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"");
			if (open.status == 200)
				break;
			std::this_thread::sleep_for(20ms);
		}
		check(open.status == 200);
		std::uint64_t sequence = 0;
		std::map<std::string, std::map<std::uint64_t, Entry>> ring;
		auto write = [&](const std::string &name, Entry ops) {
			std::string operations;
			for (const auto &[key, value] : ops) {
				operations += (operations.empty() ? "" : ",") + std::string("{\"key\":\"") + key + "\",\"value\":";
				if (!value)
					operations += "null}";
				else {
					operations += "{\"default\":" + std::to_string(value->default_value);
					if (!value->overrides.empty())
						operations += ",\"overrides\":{\"g\":" + std::to_string(std::get<std::int64_t>(value->overrides.begin()->second)) + "}";
					operations += "}}";
				}
			}
			auto reply = call("/v2/transactions", "\"generation\":1,\"sequence\":" + std::to_string(++sequence) +
													  ",\"namespace\":\"" + name + "\",\"operations\":[" + operations + "]");
			check(reply.status == 200);
			rapidjson::Document json;
			json.Parse(reply.body.c_str());
			check(std::string(json["status"].GetString()) == "applied");
			ring[name][json["namespace_revision"].GetUint64()] = std::move(ops);
		};
		auto session = [&](std::string_view token, const std::string &follows) {
			auto opened = request(endpoint, cert, "/v2/distribution/session/open",
								  "{\"version\":2,\"profile\":1,\"coding\":\"raw\",\"namespaces\":" + follows + "}", token);
			check(opened.status == 200);
			rapidjson::Document json;
			json.Parse(opened.body.c_str());
			check(json.MemberCount() == 3 && json["version"].GetUint64() == 2);
			return std::string(json["session"].GetString());
		};
		auto next = [&](std::string_view token, const std::string &id, const std::string &follows, unsigned wait,
						const std::string &extra = {}) {
			return request(endpoint, cert, "/v2/distribution/session/next",
						   "{\"version\":2,\"session\":\"" + id + "\",\"namespaces\":" + follows + extra +
							   ",\"wait_ms\":" + std::to_string(wait) + "}",
						   token);
		};
		const auto id = session(writer, "[[\"ns\"]]");
		// The first next needs positions: the leader proposes the enabling entry; an answer comes
		// once a seal follows it.
		for (unsigned j = 0;; ++j) {
			check(j < 100);
			write("ns", {{"ns:boot" + std::to_string(j), detent::KnobDef{1}}});
			auto response = next(writer, id, "[[\"ns\"]]", 50);
			if (response.status == 200)
				break;
			check(response.status == 204);
		}
		for (unsigned i = 0; i < 40; ++i) {
			const auto k = "ns:k" + std::to_string(i % 7);
			std::optional<detent::KnobDef> value;
			if (i < 7 || i % 6 != 5) {
				value = detent::KnobDef{static_cast<std::int64_t>(i)};
				if (i % 4 == 0)
					value->overrides.emplace("g", std::int64_t{-static_cast<std::int64_t>(i)});
			}
			Entry ops{{k, value}};
			if (i % 5 == 0)
				ops.emplace_back("ns:z" + std::to_string(i), detent::KnobDef{7});
			write("ns", std::move(ops));
			if (i % 8 == 0)
				write("d.x", {{"d.x:k", detent::KnobDef{static_cast<std::int64_t>(i)}}});
		}
		// The head and the ring start from a full copy, then the table on disk at that seal.
		auto copy = decode(frames_body(next(writer, id, "[[\"ns\"]]", 1000), id, false));
		const auto head = copy.frames.at(0).second.position;
		const auto incarnation = copy.frames.at(0).second.incarnation;
		const auto s = std::get<2>(copy.pieces.at(0));
		h::TableNamespace table;
		DiskFiles files;
		files.root = directory;
		files.root += ".history";
		for (auto deadline = std::chrono::steady_clock::now() + 10s;; std::this_thread::sleep_for(10ms)) {
			check(std::chrono::steady_clock::now() < deadline);
			if (!std::filesystem::exists(files.root / "layout"))
				continue;
			auto decoded = h::decode_table(files.read("layout"), {});
			if (const auto *n = decoded.find("ns"); n && n->position == s) {
				table = *n;
				break;
			}
		}
		check(table.incarnation == incarnation && head == ring["ns"].rbegin()->first);
		Oracle oracle(context.cluster, table, files, ring["ns"], head);
		auto other = d::NamespaceCursor{context.cluster, incarnation, head};
		other.incarnation[0] ^= 1;
		std::vector<std::optional<d::NamespaceCursor>> stores{std::nullopt, other};
		for (std::uint64_t r = 0; r <= head + 1; ++r)
			stores.push_back(d::NamespaceCursor{context.cluster, incarnation, r});
		unsigned full = 0, blocks = 0, entries = 0, idle = 0;
		std::uint64_t bytes = 0;
		for (const auto &store : stores) {
			std::string follows = "[[\"ns\"";
			if (store)
				follows += ",\"" + hex(store->incarnation) + "\"," + std::to_string(store->position);
			follows += "]]";
			const auto cluster = ",\"cluster\":\"" + hex(context.cluster) + "\"";
			auto response = next(writer, id, follows, 0, store ? cluster : std::string{});
			d::NamespaceRequest want{"ns", &oracle, store};
			const auto answer = d::select_answer(oracle, store);
			if (answer.kind == d::AnswerKind::Current || answer.kind == d::AnswerKind::Ahead) {
				check(response.status == 204);
				++idle;
				continue;
			}
			const auto body = frames_body(response, id, false);
			check(body == d::encode_session_frames(std::nullopt, {&want, 1})); // Byte for byte.
			bytes += body.size();
			full += answer.kind == d::AnswerKind::FullCopy;
			blocks += answer.kind == d::AnswerKind::Blocks;
			entries += answer.kind == d::AnswerKind::Entries;
		}
		check(full > 2 && blocks && entries && idle == 2);
		// zstd: the same body.
		{
			auto zid = request(endpoint, cert, "/v2/distribution/session/open",
							   "{\"version\":2,\"profile\":1,\"coding\":\"zstd\",\"namespaces\":[[\"ns\"]]}", writer);
			rapidjson::Document json;
			json.Parse(zid.body.c_str());
			const std::string zs = json["session"].GetString();
			d::NamespaceRequest want{"ns", &oracle, std::nullopt};
			check(frames_body(next(writer, zs, "[[\"ns\"]]", 0), zs, true) ==
				  d::encode_session_frames(std::nullopt, {&want, 1}));
		}
		// W13: `d.*` grants d.x; an exact grant of ns does not; `*` follows the directory.
		auto granted = decode(frames_body(next(writer, id, "[[\"d.x\"],[\"ns\"]]", 0), id, false));
		check(granted.frames.size() == 2 && granted.frames[0].first == "d.x");
		const auto rid = session(reader, "[[\"ns\"]]");
		check(next(reader, rid, "[[\"d.x\"]]", 0).status == 403);
		check(request(endpoint, cert, "/v2/distribution/session/open",
					  "{\"version\":2,\"profile\":1,\"coding\":\"raw\",\"namespaces\":[[\"d.x\"]]}", reader)
				  .status == 403);
		const auto wid = session(watcher, "[]," "\"all\":0");
		auto all = decode(frames_body(next(watcher, wid, "[]", 0, ",\"all\":0"), wid, false));
		check(all.update && all.update->entries.size() == 2 && all.frames.size() == 2);
		const auto at = all.update->to.position;
		check(next(watcher, wid, "[]", 0, ",\"cluster\":\"" + hex(context.cluster) + "\",\"all\":" + std::to_string(at))
				  .status == 204);
		// Sessions: another principal's is forbidden, a closed one has expired.
		check(next(reader, id, "[[\"ns\"]]", 0).status == 403);
		check(request(endpoint, cert, "/v2/distribution/session/close", "{\"version\":2,\"session\":\"" + id + "\"}",
					  writer)
				  .status == 204);
		check(next(writer, id, "[[\"ns\"]]", 0).status == 410);
		server.stop();
		std::filesystem::remove_all(directory);
		auto history = directory;
		history += ".history";
		std::filesystem::remove_all(history);
		std::cout << stores.size() << " cursors over HTTP byte for byte against the voter's history (" << bytes
				  << " bytes): " << full << " full copies, " << blocks << " blocks, " << entries << " entries, "
				  << idle << " idle; cut " << table.base.last << ", ring from " << s << ", head " << head << "\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
