#pragma once
// A W5/W6 test server: it parses W5 bodies with the session request parser, filters by grants (W13),
// answers from a test history source per namespace, and sends every frame in one fragment-framed
// response. A pages session gets a namespace's installed page transfer when it has no store of it,
// from the offset a next resumes it at (dense distribution D8).
#include "../detent_distribution/history_fixture.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/grants.hh"
#include "detent_distribution/session_frames.hh"
#include "detent_distribution_http/session_request.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <source_location>
#include <thread>

namespace namespace_server {
namespace d = detent::distribution;
namespace h = detent::distribution_http;
using namespace history_fixture;
using namespace std::chrono_literals;
inline void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("namespace session invariant at " + std::to_string(where.line()));
}
inline const std::string session_hex(32, '1');
inline d::Identity session_id() {
	d::Identity id;
	id.fill('\x11');
	return id;
}
// A namespace on the server: its transactions, tiled as base | one block | three ring entries.
struct Served {
	std::string name;
	d::Identity incarnation;
	std::uint64_t directory = 0;
	std::vector<Transaction> transactions;
	std::optional<d::NamespaceCursor> dropped; // gone: a follower with a store is told
	TestHistory history() const {
		const std::uint64_t head = transactions.size(), s = head >= 3 ? head - 3 : 0, p = s / 2;
		std::vector<d::HistorySpan> spans;
		if (s > p)
			spans.push_back({p + 1, s, true});
		return TestHistory(transactions, p, spans, s, incarnation);
	}
};
inline std::vector<Transaction> writes(const std::string &name, int count, int salt) {
	std::vector<Transaction> out;
	for (int i = 0; i < count; ++i) {
		Transaction tx{{name + ":k" + std::to_string((i * 7 + salt) % 5), def(i * 100 + salt, i)}};
		if (i % 4 == 3)
			tx.push_back({name + ":z" + std::to_string(i), std::nullopt}); // a delete of a key never written
		std::sort(tx.begin(), tx.end(), [](auto &a, auto &b) { return a.key < b.key; });
		out.push_back(std::move(tx));
	}
	return out;
}

// A page transfer to `answer.to` (dense distribution DD3, DD4): a full copy (`answer.after` 0) for a
// follower with no store of it, or a delta for one whose store is at `answer.after`.
struct Transfer {
	std::vector<char> bytes;
	d::PagesAnswer answer;
};
struct Server {
	std::mutex mutex;
	std::vector<Served> served; // by name
	std::map<std::string, Transfer, std::less<>> transfers;
	bool pages_session = false;
	std::vector<std::vector<std::string>> records_asked; // each open's records names
	std::vector<std::string> session_records;
	d::NamespaceGrants grants;
	bool open_session = false, corrupt = false, behind = false;
	// An older source: a repair's position (D139) is taken as no store, its floor ignored.
	bool ignore_floor = false;
	std::vector<d::NamespaceCursor> repairs; // each repair position asked, in order
	// The next reply carrying a pages frame stops after this many body bytes, once (D8).
	std::size_t cut = 0;
	std::vector<std::uint64_t> offsets; // each pages frame's offset, in order
	// A records frame of a namespace at this head names this root (dense distribution D1).
	std::map<std::string, std::pair<d::NamespaceCursor, d::RootDigest>, std::less<>> roots;
	std::atomic<unsigned> answered{0}, idle{0};
	std::exception_ptr failure;
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor{io, {asio::ip::address_v4::loopback(), 0}};
	std::shared_ptr<asio::ssl::context> tls;
	std::atomic<bool> stop{false};
	std::jthread thread;
	Server(std::shared_ptr<asio::ssl::context> context, std::initializer_list<std::string_view> patterns)
		: grants(std::span<const std::string_view>(patterns.begin(), patterns.size())), tls(std::move(context)) {
		acceptor.non_blocking(true);
		thread = std::jthread([this] { serve(); });
	}
	~Server() {
		stop = true;
	}
	Served &find(std::string_view name) {
		for (auto &each : served)
			if (each.name == name)
				return each;
		throw std::logic_error("unknown served namespace");
	}
	void serve() {
		try {
			while (!stop) {
				asio::ip::tcp::socket accepted(io);
				asio::error_code error;
				acceptor.accept(accepted, error);
				if (error == asio::error::would_block || error == asio::error::try_again) {
					std::this_thread::sleep_for(1ms);
					continue;
				}
				if (error)
					throw asio::system_error(error);
				handle(std::move(accepted));
			}
		} catch (...) {
			failure = std::current_exception();
		}
	}
	static std::string reply(std::string_view status, std::string_view headers, std::string_view body) {
		return "HTTP/1.1 " + std::string(status) + "\r\n" + std::string(headers) +
			   "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
			   std::string(body);
	}
	static std::string control(std::string_view status, std::string_view error) {
		return reply(status, "Content-Type: application/json\r\n", "{\"error\":\"" + std::string(error) + "\"}");
	}
	std::string answer(h::RequestKind kind, std::string_view body) {
		std::lock_guard lock(mutex);
		auto account = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
		auto parsed = h::parse_session_request(account, kind, body);
		const auto &request = parsed->request();
		check(request.version == 2);
		if (kind == h::RequestKind::Open) {
			open_session = true;
			pages_session = request.pages;
			session_records.assign(request.records.begin(), request.records.end());
			records_asked.push_back(session_records);
			return reply("200 OK", "Content-Type: application/json\r\n",
						 "{\"version\":2,\"session\":\"" + session_hex + "\",\"wait_ms\":1000}");
		}
		if (!open_session || request.session != session_id())
			return control("410 Gone", "session_expired");
		if (kind == h::RequestKind::Close) {
			open_session = false;
			return "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
		}
		std::map<std::string_view, std::optional<d::NamespaceCursor>> wanted;
		std::set<std::string_view> repairing;
		for (const auto &position : request.positions) {
			if (!grants.allows(position.name))
				return control("403 Forbidden", "forbidden");
			wanted[position.name] = position.cursor;
			if (position.repair) {
				repairs.push_back(*position.cursor);
				if (ignore_floor)
					wanted[position.name].reset();
				else
					repairing.insert(position.name);
			}
		}
		if (behind)
			return control("503 Service Unavailable", "source_behind");
		std::optional<d::DirectoryUpdate> directory;
		d::Identity cluster;
		cluster.fill('\x01');
		if (request.all) {
			const auto from = request.all->cluster == cluster ? request.all->position : 0;
			directory = d::DirectoryUpdate{{cluster, served.size()}, from, {}};
			for (const auto &each : served)
				if (grants.allows(each.name)) {
					wanted.try_emplace(each.name);
					if (each.directory > from)
						directory->entries.push_back({each.name, each.incarnation, true});
				}
			if (directory->entries.empty())
				directory.reset();
		}
		std::vector<TestHistory> histories;
		histories.reserve(wanted.size());
		std::deque<d::PagesAnswer> resumed;
		bool pages_frame = false;
		std::vector<d::NamespaceRequest> requests;
		for (const auto &[name, cursor] : wanted) {
			const auto &each = find(name);
			if (each.dropped) {
				if (cursor)
					requests.push_back({name, nullptr, cursor, each.dropped});
				continue;
			}
			histories.push_back(each.history());
			const auto transfer = transfers.find(name);
			const bool repair = repairing.contains(name);
			if (pages_session && transfer != transfers.end() && transfer->second.answer.to == histories.back().head() &&
				(repair	  ? !transfer->second.answer.after && transfer->second.answer.to.position >= cursor->position
				 : cursor ? cursor->same_identity(transfer->second.answer.to) &&
							   cursor->position == transfer->second.answer.after
						  : !transfer->second.answer.after) &&
				!std::binary_search(session_records.begin(), session_records.end(), name)) {
				auto &answer = resumed.emplace_back(transfer->second.answer);
				if (const auto &resume = request.resume; resume && resume->name == name && resume->root == answer.root &&
														  resume->after == answer.after && resume->offset <= answer.bytes.size())
					answer.offset = resume->offset;
				offsets.push_back(answer.offset);
				pages_frame = true;
				requests.push_back({name, &histories.back(), cursor, std::nullopt, &answer});
				requests.back().repair = repair;
			} else {
				requests.push_back({name, &histories.back(), cursor});
				requests.back().repair = repair;
				if (const auto root = roots.find(name); root != roots.end() && root->second.first == histories.back().head())
					requests.back().root = root->second.second;
			}
		}
		d::SessionFramesEncoder encoder(directory, requests);
		const bool frames = std::any_of(encoder.answers().begin(), encoder.answers().end(), [](const d::Answer &a) {
			return a.kind != d::AnswerKind::Current && a.kind != d::AnswerKind::Ahead;
		});
		if (!frames && !directory) {
			++idle;
			return "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
		}
		std::string payload;
		while (!encoder.step(payload, payload.size() + 65536)) {
		}
		std::string wire;
		d::Identity update;
		update.fill('u');
		std::size_t last_fragment = 0;
		for (std::uint64_t offset = 0; offset < payload.size() || offset == 0;) {
			last_fragment = wire.size();
			const auto n = std::min<std::uint64_t>(d::fragment_bytes, payload.size() - offset);
			d::Fragment fragment{d::UpdateKind::Frames, update, offset, payload.size(), offset + n == payload.size(),
								 payload.substr(offset, n)};
			wire += d::encode_fragment(fragment, compressed);
			offset += n;
			if (!n)
				break;
		}
		if (corrupt) // A byte in the last fragment's payload: the earlier fragments verify.
			wire[last_fragment + (wire.size() - last_fragment) / 2] ^= 0x40;
		++answered;
		auto out = reply("200 OK",
						 "Content-Type: application/vnd.detent.frames\r\nX-Detent-Session-Version: 2\r\n"
						 "X-Detent-Session: " +
							 session_hex + "\r\nX-Detent-Coding: " + (compressed ? "zstd" : "raw") + "\r\n",
						 wire);
		if (pages_frame && cut && cut < wire.size())
			out.resize(out.size() - wire.size() + std::exchange(cut, 0));
		return out;
	}
	bool compressed = false;
	void handle(asio::ip::tcp::socket accepted) {
		asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *tls);
		asio::error_code error;
		socket.handshake(asio::ssl::stream_base::server, error);
		if (error)
			return;
		asio::streambuf request;
		asio::read_until(socket, request, "\r\n\r\n", error);
		if (error)
			return;
		auto text = [&] { return std::string(asio::buffers_begin(request.data()), asio::buffers_end(request.data())); };
		const auto header = text();
		const auto body_start = header.find("\r\n\r\n") + 4;
		const auto at = header.find("Content-Length: ");
		const std::size_t length = at == std::string::npos ? 0 : std::stoul(header.substr(at + 16));
		if (request.size() < body_start + length)
			asio::read(socket, request, asio::transfer_exactly(body_start + length - request.size()), error);
		const auto body = text().substr(body_start, length);
		std::string out;
		try {
			if (header.starts_with("POST /v2/distribution/session/open "))
				out = answer(h::RequestKind::Open, body);
			else if (header.starts_with("POST /v2/distribution/session/next "))
				out = answer(h::RequestKind::Next, body);
			else if (header.starts_with("POST /v2/distribution/session/close "))
				out = answer(h::RequestKind::Close, body);
			else
				out = control("400 Bad Request", "invalid_request");
		} catch (const std::invalid_argument &) {
			out = control("400 Bad Request", "invalid_request");
		}
		asio::write(socket, asio::buffer(out), error);
		socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, error);
	}
};

} // namespace namespace_server
