// The relay's HttpUpstream reading history files in ranges (PLAN.md D138), against a TLS server that
// answers `/v2/history/file` as a voter does: the whole file only within its reply bound, a range as
// at most the bound from its offset.
// exact: a file several times the bound mirrors byte for byte, also when the relay asks ranges longer
// than the bound; a file within one range is one whole request.
// resume: an answer cut mid-file is asked again and no verified range is fetched twice, within one
// fetch and across a fetch that failed; a damaged range fails the digest and drops the prefix.
// backoff: a sync that keeps failing waits longer each time up to its bound, and reports its error.
//
//    test_http_upstream CERTIFICATES
#include "detent_relay/http_upstream.hh"
#include "detent_relay/mirror_service.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <iostream>
#include <mutex>
#include <rapidjson/document.h>
#include <reactor_tls.h>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <sys/stat.h>
#include <thread>

using namespace std::chrono_literals;
namespace d = detent::distribution;
namespace r = detent::relay;
namespace {
using Clock = std::chrono::steady_clock;
// The mirror fixture's helpers, without its SHA-256 class, which OpenSSL's `SHA256` hides here.
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error(std::string("check failed at ") + where.file_name() + ":" + std::to_string(where.line()));
}
template <class Error, class Call> void refuses(Call &&call, std::source_location where = std::source_location::current()) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	check(false, where);
}
r::Digest sha(std::string_view bytes) { return r::digest_of(bytes); }
std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
void prepare(const std::filesystem::path &root) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	check(::chmod(root.c_str(), 0700) == 0);
}
std::shared_ptr<kronuz::io::completion::PosixIO> io_at(const std::filesystem::path &root) {
	return std::make_shared<kronuz::io::completion::PosixIO>(root, 0644);
}
d::Identity identity(char fill) {
	d::Identity value;
	value.fill(fill);
	return value;
}

// A whole answer the server sent: where it starts, its length, and whether a range asked for it.
struct Served {
	std::uint64_t offset = 0, length = 0;
	bool ranged = false;
	bool operator==(const Served &) const = default;
};

class FileServer {
  public:
	FileServer(std::shared_ptr<asio::ssl::context> tls, std::size_t bound) : bound_(bound), tls_(std::move(tls)) {
		acceptor_.non_blocking(true);
		thread_ = std::jthread([this] { serve(); });
	}
	~FileServer() { stop_ = true; }
	std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
	void serving(std::string bytes) {
		std::lock_guard lock(mutex_);
		blob_ = std::move(bytes);
		served_.clear();
		cut_.clear();
	}
	std::vector<Served> served() {
		std::lock_guard lock(mutex_);
		return served_;
	}
	std::vector<std::uint64_t> cut() {
		std::lock_guard lock(mutex_);
		return cut_;
	}
	void clear() {
		std::lock_guard lock(mutex_);
		served_.clear();
		cut_.clear();
	}
	// The next answers, counted from now: the `cut`-th is cut halfway, the `damaged`-th has a byte
	// flipped, and from the `down`-th on every request answers 503.
	void plan(int cut, int damaged = -1, int down = -1) {
		std::lock_guard lock(mutex_);
		answered_ = 0;
		cut_at_ = cut;
		damaged_at_ = damaged;
		down_at_ = down;
	}
	void up() {
		std::lock_guard lock(mutex_);
		down_at_ = -1;
	}
	std::exception_ptr failure;

  private:
	static std::string reply(std::string_view status, std::string_view type, std::string_view body) {
		return "HTTP/1.1 " + std::string(status) + "\r\nContent-Type: " + std::string(type) +
			   "\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
	}
	void serve() {
		try {
			while (!stop_) {
				asio::ip::tcp::socket accepted(io_);
				asio::error_code error;
				acceptor_.accept(accepted, error);
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
	// The answer to one request, and how many of its bytes go out (all but a cut one).
	std::pair<std::string, std::size_t> answer(std::string_view header, std::string_view body) {
		std::lock_guard lock(mutex_);
		if (!header.starts_with("POST /v2/history/file "))
			return {reply("404 Not Found", "application/json", "{\"error\":\"unknown_endpoint\"}"), SIZE_MAX};
		if (down_at_ >= 0 && answered_ >= unsigned(down_at_))
			return {reply("503 Service Unavailable", "application/json", "{\"error\":\"not_ready\"}"), SIZE_MAX};
		rapidjson::Document json;
		json.Parse(body.data(), body.size());
		check(!json.HasParseError() && json.IsObject() && json.HasMember("name") &&
			  json.HasMember("offset") == json.HasMember("length"));
		Served sent{0, blob_.size(), json.HasMember("offset")};
		if (!sent.ranged && blob_.size() > bound_)
			return {reply("413 Payload Too Large", "application/json", "{\"error\":\"reply_bound\"}"), SIZE_MAX};
		if (sent.ranged) {
			sent.offset = json["offset"].GetUint64();
			if (sent.offset >= blob_.size())
				return {reply("416 Range Not Satisfiable", "application/json", "{\"error\":\"range_not_satisfiable\"}"),
						SIZE_MAX};
			sent.length = std::min<std::uint64_t>({json["length"].GetUint64(), bound_, blob_.size() - sent.offset});
		}
		auto bytes = blob_.substr(sent.offset, sent.length);
		const auto index = int(answered_++);
		if (index == damaged_at_)
			bytes[bytes.size() / 2] ^= 0x40;
		auto out = reply("200 OK", "application/octet-stream", bytes);
		if (index == cut_at_) {
			cut_.push_back(sent.offset);
			return {out, out.size() - bytes.size() / 2};
		}
		served_.push_back(sent);
		return {out, SIZE_MAX};
	}
	void handle(asio::ip::tcp::socket accepted) {
		asio::ssl::stream<asio::ip::tcp::socket> socket(std::move(accepted), *tls_);
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
		auto [out, sent] = answer(header, text().substr(body_start, length));
		asio::write(socket, asio::buffer(out.data(), std::min(sent, out.size())), error);
		socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, error);
	}

	std::size_t bound_;
	std::shared_ptr<asio::ssl::context> tls_;
	asio::io_context io_;
	asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::address_v4::loopback(), 0}};
	std::mutex mutex_;
	std::string blob_;
	std::vector<Served> served_;
	std::vector<std::uint64_t> cut_;
	unsigned answered_ = 0;
	int cut_at_ = -1, damaged_at_ = -1, down_at_ = -1;
	std::atomic<bool> stop_{false};
	std::jthread thread_;
};

// Deterministic bytes: a SHA-256 chain.
std::string blob(std::size_t size, std::string_view seed) {
	auto block = sha(seed);
	std::string out;
	while (out.size() < size) {
		out.append(block.data(), block.size());
		block = sha(std::string_view(block.data(), block.size()));
	}
	out.resize(size);
	return out;
}
// The ranges a reader of `range` bytes is answered by a server bounded at `bound`, from `from`.
std::vector<Served> ranges(std::uint64_t size, std::uint64_t range, std::uint64_t bound, std::uint64_t from = 0) {
	std::vector<Served> out;
	const auto step = std::min(range, bound);
	for (auto offset = from; offset < size; offset += step)
		out.push_back({offset, std::min(step, size - offset), true});
	return out;
}

constexpr std::size_t bound = 16384;
const std::string name = "ns/base-1";

struct Fixture {
	std::filesystem::path certificates;
	std::shared_ptr<asio::ssl::context> client;
	std::unique_ptr<FileServer> server;
	explicit Fixture(std::filesystem::path path) : certificates(std::move(path)) {
		auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*tls,
								{(certificates / "one.pem").string(), (certificates / "one.key").string(),
								 (certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		client = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
		server = std::make_unique<FileServer>(tls, bound);
	}
	r::HttpUpstream upstream(std::size_t range) const {
		r::HttpUpstreamOptions options;
		options.endpoints.emplace_back(asio::ip::address_v4::loopback(), server->port());
		options.hostname = "localhost";
		options.token = "token";
		options.tls = client;
		options.timeout = 5000ms;
		options.range_bytes = range;
		return r::HttpUpstream(std::move(options));
	}
};
r::MirrorFile file_of(const std::string &bytes) {
	return {r::MirrorFile::Kind::Base, name, 0, 10, bytes.size(), sha(bytes)};
}

void exact(Fixture &f, const std::filesystem::path &root) {
	const auto bytes = blob(5 * bound + 777, "exact");
	f.server->serving(bytes);
	auto upstream = f.upstream(bound);
	// Mirrored through the store: the file on its disk is the upstream's, byte for byte.
	prepare(root);
	{
		r::MirrorStore store(io_at(root), {});
		r::NamespaceLayout layout{"ns", identity('i'), 10, 1, 10, "policy", {file_of(bytes)}};
		const r::MirrorFetch fetch = [&](std::string_view ns, const r::MirrorFile &f) { return upstream.fetch(ns, f); };
		const auto stats = store.adopt(10, std::span(&layout, 1), {}, fetch);
		check(stats.fetched_files == 1 && stats.fetched_bytes == bytes.size());
		check(slurp(root / r::mirror_file_name(name)) == bytes);
	}
	check(f.server->served() == ranges(bytes.size(), bound, bound));
	auto stats = upstream.stats();
	check(stats.ranges == 6 && stats.fetched_files == 1 && stats.fetched_bytes == bytes.size() && !stats.failures);

	// Ranges longer than the server's bound: it answers the bound, and the reader goes on from there.
	f.server->clear();
	auto longer = f.upstream(3 * bound);
	check(longer.fetch("ns", file_of(bytes)) == bytes);
	check(f.server->served() == ranges(bytes.size(), 3 * bound, bound));

	// A file within one range: one whole request, as a relay upstream answers too.
	const auto small = blob(bound - 1, "small");
	f.server->serving(small);
	check(upstream.fetch("ns", file_of(small)) == small);
	check(f.server->served() == std::vector<Served>{{0, small.size(), false}});
	std::cout << "exact: " << bytes.size() << " bytes in " << stats.ranges << " ranges of " << bound << "\n";
}

void resume(Fixture &f) {
	const auto bytes = blob(5 * bound + 777, "resume");
	const auto file = file_of(bytes);
	f.server->serving(bytes);

	// The third answer is cut halfway: it is asked again, and nothing verified is.
	{
		auto upstream = f.upstream(bound);
		f.server->plan(2);
		check(upstream.fetch("ns", file) == bytes);
		check(f.server->cut() == std::vector<std::uint64_t>{2 * bound});
		check(f.server->served() == ranges(bytes.size(), bound, bound));
		check(upstream.stats().failures == 1 && !upstream.stats().resumed);
	}
	// The source stops answering after two ranges: the fetch fails, keeping its verified prefix, and
	// the next fetch of the same file starts where it ends.
	{
		f.server->clear();
		auto upstream = f.upstream(bound);
		f.server->plan(-1, -1, 2);
		refuses<r::UpstreamUnavailable>([&] { upstream.fetch("ns", file); });
		check(f.server->served() == ranges(2 * bound, bound, bound));
		f.server->clear();
		f.server->up();
		check(upstream.fetch("ns", file) == bytes);
		check(f.server->served() == ranges(bytes.size(), bound, bound, 2 * bound));
		check(upstream.stats().resumed == 1 && upstream.stats().fetched_bytes == bytes.size());
	}
	// A damaged range fails the file's digest: the prefix is dropped and the next fetch starts over.
	{
		f.server->clear();
		auto upstream = f.upstream(bound);
		f.server->plan(-1, 1);
		refuses<r::UpstreamUnavailable>([&] { upstream.fetch("ns", file); });
		f.server->clear();
		check(upstream.fetch("ns", file) == bytes);
		check(f.server->served() == ranges(bytes.size(), bound, bound));
		check(!upstream.stats().resumed);
	}
	// Another file drops the prefix it held.
	{
		f.server->clear();
		auto upstream = f.upstream(bound);
		f.server->plan(-1, -1, 2);
		refuses<r::UpstreamUnavailable>([&] { upstream.fetch("ns", file); });
		f.server->up();
		const auto other = blob(3 * bound, "other");
		f.server->serving(other);
		check(upstream.fetch("ns", file_of(other)) == other);
		check(f.server->served() == ranges(other.size(), bound, bound));
		check(!upstream.stats().resumed);
	}
	std::cout << "resume: ok\n";
}

// An upstream that never answers; its calls outlive the service that owns it.
struct Calls {
	std::mutex mutex;
	std::vector<Clock::time_point> at;
};
class Down final : public r::Upstream {
  public:
	explicit Down(std::shared_ptr<Calls> calls) : calls_(std::move(calls)) {}
	r::MirrorTable layouts(std::span<const std::string>) override {
		std::lock_guard lock(calls_->mutex);
		calls_->at.push_back(Clock::now());
		throw r::UpstreamUnavailable("relay upstream: connect: refused");
	}
	std::string fetch(std::string_view, const r::MirrorFile &) override { throw r::UpstreamUnavailable("down"); }
	std::string frames(std::span<const r::Position>) override { throw r::UpstreamUnavailable("down"); }

  private:
	std::shared_ptr<Calls> calls_;
};

void backoff(const std::filesystem::path &root) {
	prepare(root);
	constexpr auto interval = 5ms, maximum = 50ms, run = 1500ms;
	auto record = std::make_shared<Calls>();
	r::MirrorServiceOptions options;
	options.directory = root;
	options.cluster = identity('c');
	options.namespaces = {"ns"};
	options.interval = interval;
	options.reconnect_maximum = maximum;
	const auto started = Clock::now();
	r::MirrorStatus status;
	{
		r::MirrorService service(options, std::make_unique<Down>(record), std::make_shared<d::StorageLedger>(std::size_t{1} << 20));
		std::this_thread::sleep_for(run);
		status = service.status();
	}
	std::vector<Clock::time_point> calls;
	{
		std::lock_guard lock(record->mutex);
		calls = record->at;
	}
	// Each wait doubles from the interval (5, 10, 20, 40 ms) and then holds at the bound.
	check(calls.size() >= 10);
	for (std::size_t i = 5; i < calls.size(); ++i)
		check(calls[i] - calls[i - 1] >= maximum - 5ms);
	// The rate is bounded: at most one sync per bound after the first doublings, where a sync every
	// interval would be 300.
	const auto bound_calls = std::size_t(run / maximum) + 5;
	check(calls.size() <= bound_calls);
	check(!status.ready && !status.failed && status.sync_failures >= calls.size() - 1 &&
		  status.error.find("refused") != std::string::npos);
	std::cout << "backoff: " << calls.size() << " syncs in "
			  << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()
			  << " ms (bound " << bound_calls << "), error \"" << status.error << "\"\n";
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc < 2)
			throw std::invalid_argument("usage: test_http_upstream CERTIFICATES");
		const auto scratch = std::filesystem::current_path() / ".scratch" / ("http-upstream-" + std::to_string(getpid()));
		Fixture f(argv[1]);
		exact(f, scratch / "exact");
		resume(f);
		backoff(scratch / "backoff");
		if (f.server->failure)
			std::rethrow_exception(f.server->failure);
		std::filesystem::remove_all(scratch);
		std::cout << "http upstream: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "http upstream: " << error.what() << "\n";
		return 1;
	}
}
