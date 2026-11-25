// The relay's HttpUpstream reading history files in ranges (PLAN.md D138), against a TLS server that
// answers `/v2/history/file` as a voter does: the whole file only within its reply bound, a range as
// at most the bound from its offset.
// exact: a file several times the bound mirrors byte for byte, also when the relay asks ranges longer
// than the bound; a file within one range is one whole request.
// resume: an answer cut mid-file is asked again and no verified range is fetched twice, within one
// fetch and across a fetch that failed (a 503 or no answer); a damaged range fails the digest and drops
// the prefix.
// backoff: a sync that keeps failing waits longer each time up to its bound, and reports its error.
// chain: relay B mirrors relay A over HTTPS, reading A's base several ranges long in ranges, byte for
// byte; A answers a range from its offset, fewer bytes at the file's end. A's mirror answers late
// (each sync pauses), so a range not lost to a timeout proves the answer woke A's owner.
// idle: relay B waits on A for a change instead of polling: no sync between deadlines at rest, a
// change wakes it, and A's stop and return go through the backoff (relay-mirror-idle-wake A3).
//
//    test_http_upstream CERTIFICATES
#include "detent/owner_loop.hh"
#include "detent_relay/http_upstream.hh"
#include "detent_relay/mirror_service.hh"
#include "detent_relay/service.hh"
#include "history_format.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <cstring>
#include <functional>
#include <future>
#include <http_asio.h>
#include <http_asio_tls.h>
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
	// flipped, from the `down`-th on every request answers 503, and from the `silent`-th on every
	// answer is cut.
	void plan(int cut, int damaged = -1, int down = -1, int silent = -1) {
		std::lock_guard lock(mutex_);
		answered_ = 0;
		cut_at_ = cut;
		damaged_at_ = damaged;
		down_at_ = down;
		silent_at_ = silent;
	}
	void up() {
		std::lock_guard lock(mutex_);
		down_at_ = silent_at_ = -1;
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
		if (index == cut_at_ || (silent_at_ >= 0 && index >= silent_at_)) {
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
	int cut_at_ = -1, damaged_at_ = -1, down_at_ = -1, silent_at_ = -1;
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
	// The source stops answering after two ranges (every answer cut): the same, a lost answer kept
	// apart from a 503.
	{
		f.server->clear();
		auto upstream = f.upstream(bound);
		f.server->plan(-1, -1, -1, 2);
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
// A voter's history for the chain: one namespace, a base of `keys` real records; publish() seals a
// base one record longer, the change a mirror reading it adopts; reseal() seals the same base.
class Voter final : public r::Upstream {
  public:
	explicit Voter(std::size_t keys) { rebase(keys); }
	r::MirrorTable layouts(std::span<const std::string>) override {
		std::this_thread::sleep_for(std::chrono::milliseconds(sync_ms.load()));
		std::lock_guard lock(mutex_);
		return {0, seal_, {{"ns", identity('i'), seal_, seal_, current_.last, "policy-1", {current_}}}};
	}
	std::string fetch(std::string_view, const r::MirrorFile &) override {
		std::lock_guard lock(mutex_);
		return held_;
	}
	std::string frames(std::span<const r::Position>) override { return d::encode_session_frames(std::nullopt, {}); }
	void publish() {
		std::lock_guard lock(mutex_);
		rebase(current_.last + 1);
		++seal_;
	}
	void reseal() {
		std::lock_guard lock(mutex_);
		++seal_;
	}
	std::string bytes; // the first base's
	r::MirrorFile file;
	std::atomic<std::chrono::milliseconds::rep> sync_ms{0}; // each sync's pause on the mirror thread

  private:
	void rebase(std::size_t keys) {
		namespace h = detent::authority::history;
		h::Block base{"ns", identity('i'), 0, keys, {}};
		for (std::size_t i = 0; i < keys; ++i) {
			char key[16];
			std::snprintf(key, sizeof key, "ns:k%06zu", i);
			const detent::KnobDef definition{std::int64_t(i)};
			base.changes.push_back({key, i + 1, detent::authority::encode_definition(definition)});
		}
		held_ = h::encode_base(base);
		current_ = {r::MirrorFile::Kind::Base, "base-ns-" + std::to_string(keys), 0, keys, held_.size(), sha(held_)};
		if (bytes.empty()) {
			bytes = held_;
			file = current_;
		}
	}
	std::mutex mutex_;
	std::uint64_t seal_ = 2;
	std::string held_;
	r::MirrorFile current_;
};

// Relay A as the relay host runs it: its mirror of the voter behind ServiceRuntime and the HTTPS
// listener, the runtime stepped on its owner loop in the host's turn shape, torn down in the host's
// phases. It has no health turn: an idle round sleeps until a wake, so a missed wake stalls the chain.
// Slow syncs hold each answer on the mirror thread past the owner's round, so only its wake ends it.
class RelayA {
  public:
	RelayA(const Fixture &f, const std::filesystem::path &root, std::size_t keys, std::uint16_t port = 0)
		: voter_(new Voter(keys)) {
		bytes = voter_->bytes;
		file = voter_->file;
		prepare(root);
		r::MirrorServiceOptions options;
		options.directory = root;
		options.cluster = identity('c');
		options.namespaces = {"ns"};
		options.interval = 10ms;
		auto ledger = [](std::size_t limit) { return std::make_shared<d::StorageLedger>(limit); };
		mirror_ = std::make_shared<r::MirrorService>(options, std::unique_ptr<r::Upstream>(voter_), ledger(1 << 30));
		r::ConfiguredCredential credential;
		const auto digest = sha(token);
		std::memcpy(credential.token_hash.data(), digest.data(), digest.size());
		credential.principal.identity[0] = 'b';
		credential.principal.policy_epoch = 1;
		const std::string_view patterns[]{"ns"};
		credential.namespaces = std::make_shared<const d::NamespaceGrants>(patterns);
		auto registry = std::make_shared<r::Registry>(ledger(1 << 20), std::span(&credential, 1));
		runtime_ = std::make_shared<r::ServiceRuntime>(
			r::ServiceAccounts{ledger(1 << 20), {ledger(1 << 24), ledger(1 << 24), ledger(1 << 20)}}, registry,
			mirror_, r::ServiceLimits{}, [wake = owner_.wake()] { wake.raise(); });
		facade_ = std::make_unique<r::Service>(runtime_);
		auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*tls,
								{(f.certificates / "one.pem").string(), (f.certificates / "one.key").string(),
								 (f.certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		listener_ = std::make_unique<http::HttpAsioService>(*facade_, 1, 64, http::tls_connection_factory(tls), 64);
		http::AsioBindOptions bind;
		bind.address = "127.0.0.1";
		listener_->set_bind_options(bind);
		listener_->start(port);
		turn_ = [this] {
			if (owner_.wake().begin_turn() || round_)
				runtime_->rescan();
			round_ = false;
			const bool progress = runtime_->step(Clock::now());
			if (stopped_ && runtime_->drained()) {
				owner_.wake().release();
				drained_.set_value();
				return;
			}
			if (progress) {
				round_ = true;
				asio::post(owner_.io(), turn_);
				return;
			}
			const bool idle = runtime_->swept();
			round_ = idle;
			owner_.wake().arm(tick_, Clock::now() + (idle ? std::chrono::milliseconds(1h) : 1ms));
			tick_.async_wait([this](const asio::error_code &error) {
				if (!error || error == asio::error::operation_aborted)
					turn_();
			});
		};
		asio::post(owner_.io(), turn_);
	}
	~RelayA() {
		runtime_->close();
		mirror_->close();
		listener_->stop();
		asio::post(owner_.io(), [this] {
			stopped_ = true;
			owner_.wake().raise();
		});
		drained_.get_future().wait_for(10s);
		owner_.stop();
		listener_->close();
		owner_.close();
		listener_->destroy_roots();
		owner_.destroy_roots();
		facade_.reset();
		runtime_.reset();
		mirror_.reset();
	}
	std::uint16_t port() const { return listener_->port(); }
	r::MirrorStatus status() const { return mirror_->status(); }
	void slow_syncs(std::chrono::milliseconds pause) { voter_->sync_ms.store(pause.count()); }
	void publish() { voter_->publish(); }
	void reseal() { voter_->reseal(); }
	// Reseals and returns once A's mirror serves the new seal (A syncs its voter every 10 ms).
	void reseal_adopted() {
		const auto before = status().seal;
		reseal();
		until_seal([&] { return status().seal > before; });
	}
	static constexpr std::string_view token = "0123456789abcdef0123456789abcdef";
	std::string bytes;
	r::MirrorFile file;

  private:
	template <class Done> static void until_seal(Done done) {
		for (const auto limit = Clock::now() + 30s; !done(); std::this_thread::sleep_for(1ms))
			if (Clock::now() > limit)
				throw std::runtime_error("timed out waiting for relay A's seal");
	}
	Voter *voter_; // owned by the mirror
	std::shared_ptr<r::MirrorService> mirror_;
	std::shared_ptr<r::ServiceRuntime> runtime_;
	std::unique_ptr<r::Service> facade_;
	detent::OwnerLoop owner_;
	asio::steady_timer tick_{owner_.io()};
	std::unique_ptr<http::HttpAsioService> listener_;
	std::function<void()> turn_;
	bool stopped_ = false, round_ = true;
	std::promise<void> drained_;
};

template <class Done> void until(Done done, const char *what) {
	for (const auto limit = Clock::now() + 30s; !done(); std::this_thread::sleep_for(1ms))
		if (Clock::now() > limit)
			throw std::runtime_error(std::string("timed out waiting for ") + what);
}

void chain(Fixture &f, const std::filesystem::path &root) {
	constexpr std::size_t range = 4096;
	RelayA a(f, root / "a", 2000);
	until([&] { return a.status().ready; }, "relay A's first sync");
	check(a.file.size > 4 * range);
	a.slow_syncs(20ms);
	auto options = [&](std::size_t bytes) {
		r::HttpUpstreamOptions o;
		o.endpoints.emplace_back(asio::ip::address_v4::loopback(), a.port());
		o.hostname = "localhost";
		o.token = std::string(RelayA::token);
		o.tls = f.client;
		o.timeout = 5000ms;
		o.range_bytes = bytes;
		return o;
	};
	// One reader: the ranges join into the file, the last one shorter.
	{
		r::HttpUpstream upstream(options(range));
		check(upstream.fetch("ns", a.file) == a.bytes);
		const auto stats = upstream.stats();
		check(stats.ranges == (a.file.size + range - 1) / range && stats.fetched_bytes == a.file.size);
		check(stats.failures == 0); // a range answered late on A's mirror thread still woke A's owner
	}
	// Relay B mirrors A: the file on its disk is the voter's, byte for byte.
	prepare(root / "b");
	r::MirrorServiceOptions b;
	b.directory = root / "b";
	b.cluster = identity('c');
	b.namespaces = {"ns"};
	b.interval = 10ms;
	r::MirrorStatus status;
	{
		r::MirrorService relay(b, std::make_unique<r::HttpUpstream>(options(range)),
							   std::make_shared<d::StorageLedger>(std::size_t{1} << 30));
		until([&] { return relay.status().ready || relay.status().failed; }, "relay B's first sync");
		status = relay.status();
	}
	check(status.ready && status.fetched_files == 1 && status.fetched_bytes == a.file.size);
	check(slurp(root / "b" / r::mirror_file_name(a.file.name)) == a.bytes);
	std::cout << "chain: B mirrored A's " << a.file.size << "-byte base in ranges of " << range << "\n";
}
// idle: relay B waits on A's parked next and syncs only when something changed or its wait's
// deadline passed. Idle, it makes no sync between deadlines, where polling every 10 ms makes dozens;
// A's change wakes it at once; with A gone it fails with the backoff, and syncs again when A returns.
// An upstream whose first wait fails, as at a source that hangs: it counts the moves past a source
// and the count each sync's layouts saw.
class FailingWait final : public r::Upstream {
  public:
	explicit FailingWait(std::size_t keys) : voter_(keys) {}
	r::MirrorTable layouts(std::span<const std::string> names) override {
		seen_skips = skips.load();
		return voter_.layouts(names);
	}
	std::string fetch(std::string_view name, const r::MirrorFile &file) override { return voter_.fetch(name, file); }
	std::string frames(std::span<const r::Position> positions) override { return voter_.frames(positions); }
	std::size_t sources() const noexcept override { return 2; }
	void skip() override { ++skips; }
	bool wait(std::span<const r::Position>, std::chrono::milliseconds limit, std::stop_token stop) override {
		if (waits++ == 0)
			throw std::runtime_error("the source hangs");
		const auto end = Clock::now() + limit;
		while (!stop.stop_requested() && Clock::now() < end)
			std::this_thread::sleep_for(5ms);
		return false;
	}
	std::atomic<int> waits{0}, skips{0}, seen_skips{0};

  private:
	Voter voter_;
};
// A failed wait moves the mirror past its source before the sync it wakes, so that sync reads the
// next source rather than meeting the failed one again.
void failed_wait(const std::filesystem::path &root) {
	prepare(root);
	r::MirrorServiceOptions options;
	options.directory = root;
	options.cluster = identity('c');
	options.namespaces = {"ns"};
	options.interval = 10ms;
	options.maximum_wait_ms = 4000;
	auto *upstream = new FailingWait(4);
	r::MirrorService mirror(options, std::unique_ptr<r::Upstream>(upstream),
							std::make_shared<d::StorageLedger>(std::size_t{1} << 30));
	until([&] { return upstream->waits.load() >= 2; }, "the mirror waiting again after a failed wait");
	const auto syncs = mirror.status().syncs;
	std::cout << "failed wait: " << upstream->skips << " skip, the sync after it saw " << upstream->seen_skips
			  << ", " << syncs << " syncs\n";
	check(upstream->skips == 1 && upstream->seen_skips == 1 && syncs == 2);
}
// An idle relay asks its upstream at most once an interval, however short the timeout makes each
// parked wait (timeout 400 ms parks 200 ms; interval 1 s).
void paced(Fixture &f, const std::filesystem::path &root) {
	auto a = std::make_unique<RelayA>(f, root / "a", 200);
	until([&] { return a->status().ready; }, "relay A's first sync");
	r::HttpUpstreamOptions upstream;
	upstream.endpoints.emplace_back(asio::ip::address_v4::loopback(), a->port());
	upstream.hostname = "localhost";
	upstream.token = std::string(RelayA::token);
	upstream.tls = f.client;
	upstream.timeout = 400ms;
	prepare(root / "b");
	r::MirrorServiceOptions options;
	options.directory = root / "b";
	options.cluster = identity('c');
	options.namespaces = {"ns"};
	options.interval = 1000ms;
	r::MirrorService b(options, std::make_unique<r::HttpUpstream>(upstream),
					   std::make_shared<d::StorageLedger>(std::size_t{1} << 30));
	until([&] { return b.status().syncs >= 2; }, "relay B's sync after its watch session opened");
	const auto idle = a->status().idle;
	const auto syncs = b.status().syncs;
	std::this_thread::sleep_for(3000ms);
	const auto answered = a->status().idle - idle;
	std::cout << "paced: A answered B " << answered << " idle waits and B synced " << b.status().syncs - syncs
			  << " times in 3 s (interval 1 s, each wait 200 ms)\n";
	check(answered >= 2 && answered <= 4 && b.status().syncs - syncs == 0);
}
// B's upstream, with a hook its watcher runs once before a wait: a test orders A's seal against
// B's next by the hook, not by timing.
class Hooked final : public r::Upstream {
  public:
	explicit Hooked(r::HttpUpstreamOptions options) : inner_(std::move(options)) {}
	r::MirrorTable layouts(std::span<const std::string> names) override { return inner_.layouts(names); }
	std::string fetch(std::string_view name, const r::MirrorFile &file) override { return inner_.fetch(name, file); }
	std::string frames(std::span<const r::Position> positions) override { return inner_.frames(positions); }
	std::size_t sources() const noexcept override { return inner_.sources(); }
	void skip() override { inner_.skip(); }
	bool wait(std::span<const r::Position> positions, std::chrono::milliseconds limit, std::stop_token stop) override {
		std::function<void()> hook;
		{
			std::lock_guard lock(mutex_);
			hook = std::exchange(hook_, nullptr);
		}
		if (hook)
			hook();
		return inner_.wait(positions, limit, stop);
	}
	void before_wait(std::function<void()> hook) {
		std::lock_guard lock(mutex_);
		hook_ = std::move(hook);
	}

  private:
	r::HttpUpstream inner_;
	std::mutex mutex_;
	std::function<void()> hook_;
};
// Waits until B serves A's seal `target`, timed from A adopting it: within 1.5 s, against B's 4 s
// deadline.
void seal_reaches(r::MirrorService &b, std::uint64_t target, Clock::time_point from, const char *what) {
	until([&] { return b.status().seal >= target; }, what);
	const auto reached = Clock::now() - from;
	std::cout << "idle: " << what << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(reached).count()
			  << " ms\n";
	check(reached < 1500ms);
}
void idle(Fixture &f, const std::filesystem::path &root) {
	constexpr auto deadline = 4000ms;
	auto a = std::make_unique<RelayA>(f, root / "a", 200);
	until([&] { return a->status().ready; }, "relay A's first sync");
	const auto port = a->port();
	r::HttpUpstreamOptions upstream;
	upstream.endpoints.emplace_back(asio::ip::address_v4::loopback(), port);
	upstream.hostname = "localhost";
	upstream.token = std::string(RelayA::token);
	upstream.tls = f.client;
	upstream.timeout = 2000ms;
	prepare(root / "b");
	r::MirrorServiceOptions options;
	options.directory = root / "b";
	options.cluster = identity('c');
	options.namespaces = {"ns"};
	options.interval = 10ms;
	options.reconnect_maximum = 400ms;
	options.maximum_wait_ms = deadline.count();
	auto *hooked = new Hooked(upstream);
	// Open: A seals after B's first sync and before B's watch session opens, which A stamps with that
	// seal. B's first wait reports a change, so B syncs the seal at once.
	const auto first = a->status().seal;
	std::promise<Clock::time_point> opened;
	hooked->before_wait([&] {
		a->reseal_adopted();
		opened.set_value(Clock::now());
	});
	r::MirrorService b(options, std::unique_ptr<r::Upstream>(hooked),
					   std::make_shared<d::StorageLedger>(std::size_t{1} << 30));
	seal_reaches(b, first + 1, opened.get_future().get(), "a seal before B's watch session opened reached B");
	// Idle: between deadlines B syncs nothing; the idle 204 each second waits again.
	until([&] { return a->status().parked == 1; }, "B's next parked at A");
	const auto rested = b.status().syncs;
	std::this_thread::sleep_for(1500ms);
	const auto idle_syncs = b.status().syncs - rested;
	std::cout << "idle: B synced " << idle_syncs << " times in 1.5 s at rest (interval 10 ms, deadline 4 s)\n";
	check(idle_syncs == 0);
	// A change wakes B well before its deadline: it fetches A's new base.
	const auto fetched = b.status().fetched_files;
	const auto changed = Clock::now();
	a->publish();
	until([&] { return b.status().fetched_files > fetched; }, "relay B adopting A's change");
	const auto woke = Clock::now() - changed;
	std::cout << "idle: A's change reached B in " << std::chrono::duration_cast<std::chrono::milliseconds>(woke).count()
			  << " ms\n";
	check(woke < 1500ms);
	// With nothing changing, B's next sync is its deadline's.
	const auto settled = b.status().syncs;
	const auto quiet = Clock::now();
	until([&] { return b.status().syncs > settled; }, "relay B's deadline sync");
	const auto waited = Clock::now() - quiet;
	std::cout << "idle: the next sync came after " << std::chrono::duration_cast<std::chrono::milliseconds>(waited).count()
			  << " ms\n";
	check(waited >= deadline - 1500ms && waited < deadline + 2000ms);
	// Parked: a seal that moves no head answers B's parked next 204 at once.
	until([&] { return a->status().parked == 1; }, "B's next parked at A");
	std::promise<Clock::time_point> between;
	hooked->before_wait([&] {
		a->reseal_adopted();
		between.set_value(Clock::now());
	});
	const auto parked_seal = a->status().seal + 1;
	const auto resealed = Clock::now();
	a->reseal_adopted();
	seal_reaches(b, parked_seal, resealed, "a seal while B's next was parked reached B");
	// Stamped older: the hook seals A after B's sync read its layouts and before B's next reaches A, the
	// window CI's sanitizers hit. A answers that next 204 at once: B's session was last answered before.
	seal_reaches(b, parked_seal + 1, between.get_future().get(), "a seal between B's sync and its next reached B");
	// A stops: B's wait fails, and B's syncs fail with the backoff (10 ms doubling to 400 ms) rather than
	// every interval; A returns on its port and B syncs from it again.
	const auto failures = b.status().sync_failures;
	const auto stopped = Clock::now();
	a.reset();
	std::this_thread::sleep_for(1500ms);
	const auto down = b.status();
	const auto gone = Clock::now() - stopped;
	const auto failed = down.sync_failures - failures;
	std::cout << "idle: A down " << std::chrono::duration_cast<std::chrono::milliseconds>(gone).count() << " ms, B failed "
			  << failed << " syncs: \"" << down.error << "\"\n";
	// 20 + 40 + ... + 320 ms, then one per 400 ms.
	check(failed >= 2 && failed <= std::uint64_t(gone / 400ms) + 6);
	// A returns as it was, its change and seals published, so its seal is B's and not one B refuses as older.
	a = std::make_unique<RelayA>(f, root / "a", 200, port);
	a->publish();
	a->reseal();
	a->reseal();
	a->reseal();
	const auto back = b.status().syncs;
	try {
		until([&] { return b.status().syncs > back && b.status().error.empty(); }, "relay B syncing from A again");
	} catch (const std::runtime_error &error) {
		throw std::runtime_error(std::string(error.what()) + ": \"" + b.status().error + "\"");
	}
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
		chain(f, scratch / "chain");
		idle(f, scratch / "idle");
		failed_wait(scratch / "failed-wait");
		paced(f, scratch / "paced");
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
