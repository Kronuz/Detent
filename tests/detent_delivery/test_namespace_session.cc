// namespace-s6 A3: the agent's client side speaks W5/W6 against a test server. The server parses W5
// bodies with the session request parser, filters by grants (W13), answers from a test history
// source per namespace, and sends every frame in one fragment-framed response.
#include "../detent_distribution/history_fixture.hh"
#include "detent_delivery/namespace_session.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/grants.hh"
#include "detent_distribution_http/session_request.hh"
#include "detent_transport/await_io.hh"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <source_location>
#include <thread>

namespace d = detent::distribution;
namespace h = detent::distribution_http;
namespace t = detent::delivery;
namespace transport = detent::transport;
using namespace history_fixture;
using namespace std::chrono_literals;

namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("namespace session invariant at " + std::to_string(where.line()));
}
const std::string session_hex(32, '1');
d::Identity session_id() {
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
std::vector<Transaction> writes(const std::string &name, int count, int salt) {
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

struct Server {
	std::mutex mutex;
	std::vector<Served> served; // by name
	d::NamespaceGrants grants;
	bool open_session = false, corrupt = false, behind = false;
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
		for (const auto &position : request.positions) {
			if (!grants.allows(position.name))
				return control("403 Forbidden", "forbidden");
			wanted[position.name] = position.cursor;
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
		std::vector<d::NamespaceRequest> requests;
		for (const auto &[name, cursor] : wanted) {
			const auto &each = find(name);
			if (each.dropped) {
				if (cursor)
					requests.push_back({name, nullptr, cursor, each.dropped});
				continue;
			}
			histories.push_back(each.history());
			requests.push_back({name, &histories.back(), cursor});
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
		return reply("200 OK",
					 "Content-Type: application/vnd.detent.frames\r\nX-Detent-Session-Version: 2\r\n"
					 "X-Detent-Session: " +
						 session_hex + "\r\nX-Detent-Coding: " + (compressed ? "zstd" : "raw") + "\r\n",
					 wire);
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

// The agent's store as the frames build it: per namespace, its cursor and records. A response's
// frames are prepared per namespace and published together, which bumps each namespace's change word.
struct Store final : t::FramePreparer {
	std::map<std::string, std::pair<d::NamespaceCursor, State>> namespaces;
	std::map<std::string, unsigned> change;
	std::optional<d::DirectoryCursor> directory_at;
	std::vector<d::DirectoryEntry> created;
	std::map<std::string, d::FrameKind> kinds; // the last frame kind per namespace
	unsigned prepared = 0;
	class Frame final : public Prepared {
	  public:
		Frame(Store &owner, std::string name, d::FrameKind kind, d::NamespaceCursor to)
			: store_(owner), name_(std::move(name)), kind_(kind), to_(to) {
			if (const auto found = store_.namespaces.find(name_); found != store_.namespaces.end())
				state_ = found->second.second;
		}
		void piece(d::PieceKind kind, std::uint64_t, std::uint64_t) override {
			if (kind == d::PieceKind::FullCopy)
				state_.clear();
		}
		void record(std::string_view key, std::uint64_t revision, const KnobDef *value) override {
			if (value)
				state_[std::string(key)] = {revision, *value};
			else
				state_.erase(std::string(key)); // a tombstone for a key the store lacks is a no-op
		}
		std::size_t bytes() const noexcept override { return 64 + 64 * state_.size(); }
		void publish() override {
			if (kind_ == d::FrameKind::Dropped)
				store_.namespaces.erase(name_);
			else
				store_.namespaces[name_] = {to_, std::move(state_)};
			++store_.change[name_];
			store_.kinds[name_] = kind_;
		}

	  private:
		Store &store_;
		std::string name_;
		d::FrameKind kind_;
		d::NamespaceCursor to_;
		State state_;
	};
	std::unique_ptr<Prepared> prepare(std::string_view name, d::FrameKind kind, std::uint64_t after,
									  const d::NamespaceCursor &to) override {
		++prepared;
		if (kind == d::FrameKind::Entries || kind == d::FrameKind::Blocks) {
			const auto found = namespaces.find(std::string(name));
			check(found != namespaces.end() && found->second.first.same_identity(to) &&
				  found->second.first.position == after);
		}
		return std::make_unique<Frame>(*this, std::string(name), kind, to);
	}
	void directory(const d::DirectoryUpdate &update) override {
		directory_at = update.to;
		created.insert(created.end(), update.entries.begin(), update.entries.end());
	}
	t::Following following(bool all) const {
		t::Following out;
		for (const auto &[name, entry] : namespaces)
			out.namespaces.push_back({name, entry.first});
		if (all)
			out.all = directory_at.value_or(d::DirectoryCursor{});
		return out;
	}
	bool operator==(const Store &other) const {
		return namespaces == other.namespaces && change == other.change && directory_at == other.directory_at &&
			   created == other.created && kinds == other.kinds;
	}
};

template <class Error> bool fails(asio::io_context &io, task::asio_scheduler &scheduler, auto work) {
	try {
		io.restart();
		transport::run_until_done(io, scheduler, std::move(work));
	} catch (const Error &) {
		return true;
	}
	return false;
}

void run(const std::filesystem::path &certificates, bool compressed) {
	auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*server_tls,
							{(certificates / "one.pem").string(), (certificates / "one.key").string(),
							 (certificates / "ca.pem").string()},
							reactor::tls::Role::Server);
	auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
	reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
	Server server(server_tls, {"app", "pay.*"});
	server.compressed = compressed;
	server.served = {{"app", id('a'), 1, writes("app", 5, 1)},
					 {"pay.eu", id('e'), 2, writes("pay.eu", 8, 2)},
					 {"pay.us", id('u'), 3, writes("pay.us", 2, 3)},
					 {"secret", id('s'), 4, writes("secret", 4, 4)}};
	asio::io_context io;
	task::asio_scheduler scheduler(io.get_executor());
	t::ClientOptions options;
	options.scheduler = &scheduler;
	options.endpoint = server.acceptor.local_endpoint();
	options.hostname = "localhost";
	options.token = "0123456789abcdef0123456789abcdef";
	options.tls_context = client_tls;
	options.compression = compressed;
	options.timeout = 5s;
	Store store;
	auto matches_server = [&] {
		std::lock_guard lock(server.mutex);
		for (const auto &each : server.served) {
			if (!server.grants.allows(each.name) || each.dropped) {
				check(!store.namespaces.contains(each.name));
				continue;
			}
			const auto history = each.history();
			const auto &[cursor, state] = store.namespaces.at(each.name);
			check(cursor == history.head());
			check(state == history.state_at(history.head().position));
		}
	};

	// A first start: no store; `all` from the directory's start gets every granted namespace.
	t::NamespaceSession session = transport::run_until_done(io, scheduler, t::open_namespace_session(options, store.following(true)));
	check(session.valid() && session.maximum_wait == 1000ms);
	io.restart();
	auto outcome = transport::run_until_done(
		io, scheduler, t::next_namespace_session(options, session, store.following(true), 0ms, store));
	check(outcome == t::FramesOutcome::Frames && store.namespaces.size() == 3 && !store.namespaces.contains("secret"));
	check(store.directory_at && store.directory_at->position == 4 && store.created.size() == 3);
	for (const auto &[name, kind] : store.kinds)
		check(kind == d::FrameKind::FullCopy);
	matches_server();

	// The server moves on: pay.eu by ten (below its new ring: a blocks answer), app by one (an entry).
	{
		std::lock_guard lock(server.mutex);
		auto &eu = server.find("pay.eu").transactions;
		for (auto &tx : writes("pay.eu", 10, 5))
			eu.push_back(std::move(tx));
		server.find("app").transactions.push_back({{"app:k9", def(9, 0)}});
	}
	store.kinds.clear();
	io.restart();
	outcome = transport::run_until_done(
		io, scheduler, t::next_namespace_session(options, session, store.following(true), 0ms, store));
	check(outcome == t::FramesOutcome::Frames && store.kinds.size() == 2);
	check(store.kinds.at("pay.eu") == d::FrameKind::Blocks && store.kinds.at("app") == d::FrameKind::Entries);
	matches_server();

	// Everything current: next answers idle.
	io.restart();
	outcome = transport::run_until_done(
		io, scheduler, t::next_namespace_session(options, session, store.following(true), 0ms, store));
	check(outcome == t::FramesOutcome::Idle && server.idle == 1);

	// A namespace the credential does not grant is refused.
	t::Following secret{{{"secret", {}}}, {}};
	check(fails<t::SessionResetError>(io, scheduler, t::next_namespace_session(options, session, secret, 0ms, store)));

	// A response of two namespaces over two fragments, the first complete in fragment 1 and the
	// second corrupt in fragment 2: nothing of it is published, and the ledger is back to its use.
	{
		std::lock_guard lock(server.mutex);
		for (auto name : {"app", "pay.us"}) {
			Transaction big;
			for (int i = 0; i < 1500; ++i) {
				char key[32];
				std::snprintf(key, sizeof key, "%s:big%04d", name, i);
				big.push_back({key, def(i * 10, 0)});
			}
			server.find(name).transactions.push_back(std::move(big));
		}
		server.corrupt = true;
	}
	const auto before = store;
	const auto prepared_before = store.prepared;
	{
		auto ledger = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
		auto limited = options;
		limited.limits.storage_account = ledger;
		check(fails<std::invalid_argument>(
			io, scheduler, t::next_namespace_session(limited, session, store.following(true), 0ms, store)));
		check(store.prepared >= prepared_before + 2); // Both frames reached the preparer.
		check(store == before && ledger->peak() > 0 && ledger->used() == 0);
	}
	server.corrupt = false;
	std::size_t peak = 0;
	{
		auto ledger = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
		auto limited = options;
		limited.limits.storage_account = ledger;
		io.restart();
		outcome = transport::run_until_done(
			io, scheduler, t::next_namespace_session(limited, session, store.following(true), 0ms, store));
		check(outcome == t::FramesOutcome::Frames && ledger->used() == 0 && store.change.at("app") > before.change.at("app"));
		peak = ledger->peak();
	}
	matches_server();

	// A ledger at its bound refuses the answer (R8): at once, and part way through the staging.
	{
		std::lock_guard lock(server.mutex);
		server.find("app").transactions.push_back({{"app:k8", def(8, 0)}});
		for (auto name : {"app", "pay.us"}) {
			Transaction big;
			for (int i = 0; i < 1500; ++i) {
				char key[32];
				std::snprintf(key, sizeof key, "%s:big%04d", name, i);
				big.push_back({key, def(i * 10 + 1, 0)});
			}
			server.find(name).transactions.push_back(std::move(big));
		}
	}
	const auto unrefused = store;
	for (auto limit : {std::size_t{512}, peak / 2}) {
		auto ledger = std::make_shared<d::StorageLedger>(limit);
		auto limited = options;
		limited.limits.storage_account = ledger;
		check(fails<d::StoragePressure>(
			io, scheduler, t::next_namespace_session(limited, session, store.following(true), 0ms, store)));
		check(store == unrefused && ledger->used() == 0);
	}
	io.restart();
	outcome = transport::run_until_done(
		io, scheduler, t::next_namespace_session(options, session, store.following(true), 0ms, store));
	check(outcome == t::FramesOutcome::Frames);
	matches_server();

	// A voter that cannot freeze a live namespace answers 503 source_behind: the store keeps its cursors.
	{
		std::lock_guard lock(server.mutex);
		server.find("app").transactions.push_back({{"app:k7", def(7, 0)}});
		server.behind = true;
	}
	const auto kept = store;
	try {
		io.restart();
		transport::run_until_done(io, scheduler,
								  t::next_namespace_session(options, session, store.following(true), 0ms, store));
		check(false);
	} catch (const t::SessionResetError &error) {
		check(error.reason() == t::SessionResetReason::SourceBehind);
	}
	check(store == kept);
	server.behind = false;
	io.restart();
	check(transport::run_until_done(io, scheduler,
									t::next_namespace_session(options, session, store.following(true), 0ms, store)) ==
		  t::FramesOutcome::Frames);
	matches_server();

	// A followed namespace that was dropped is signalled: the store learns of it and forgets it.
	{
		std::lock_guard lock(server.mutex);
		auto &gone = server.find("pay.us");
		gone.dropped = d::NamespaceCursor{d::Identity{}, gone.incarnation, gone.transactions.size() + 1};
		gone.dropped->cluster.fill('\x01');
	}
	check(store.namespaces.contains("pay.us"));
	io.restart();
	outcome = transport::run_until_done(
		io, scheduler, t::next_namespace_session(options, session, store.following(true), 0ms, store));
	check(outcome == t::FramesOutcome::Frames && !store.namespaces.contains("pay.us") &&
		  store.kinds.at("pay.us") == d::FrameKind::Dropped);
	matches_server();
	// Told once: it is no longer followed, and nothing else changed.
	io.restart();
	check(transport::run_until_done(io, scheduler,
									t::next_namespace_session(options, session, store.following(true), 0ms, store)) ==
		  t::FramesOutcome::Idle);

	// Close, then a next on the closed session is a reset.
	io.restart();
	transport::run_until_done(io, scheduler, t::close_namespace_session(options, session));
	try {
		io.restart();
		transport::run_until_done(io, scheduler,
								  t::next_namespace_session(options, session, store.following(true), 0ms, store));
		check(false);
	} catch (const t::SessionResetError &error) {
		check(error.reason() == t::SessionResetReason::Expired);
	}
	server.stop = true;
	server.thread.join();
	if (server.failure)
		std::rethrow_exception(server.failure);
}

void request_bodies() {
	d::Identity cluster, other, incarnation;
	cluster.fill('\x01');
	other.fill('\x07');
	incarnation.fill('\x22');
	t::Following following{{{"a", d::NamespaceCursor{cluster, incarnation, 5}},
							{"b", {}},
							{"c", d::NamespaceCursor{other, incarnation, 9}}},
						   d::DirectoryCursor{cluster, 3}};
	const auto ones = std::string(32, '0').replace(0, 32, "01010101010101010101010101010101");
	const auto twos = std::string("22222222222222222222222222222222");
	check(t::namespace_session_body(t::SessionResponsePurpose::Open, following, true) ==
		  "{\"version\":2,\"profile\":1,\"coding\":\"zstd\",\"cluster\":\"" + ones + "\",\"namespaces\":[[\"a\",\"" +
			  twos + "\",5],[\"b\"],[\"c\"]],\"all\":3}");
	check(t::namespace_session_body(t::SessionResponsePurpose::Next, following, false, session_id(), 250ms) ==
		  "{\"version\":2,\"session\":\"" + session_hex + "\",\"cluster\":\"" + ones +
			  "\",\"namespaces\":[[\"a\",\"" + twos + "\",5],[\"b\"],[\"c\"]],\"all\":3,\"wait_ms\":250}");
	check(t::namespace_session_body(t::SessionResponsePurpose::Close, {}, false, session_id()) ==
		  "{\"version\":2,\"session\":\"" + session_hex + "\"}");
	// What the client writes, the server's parser reads back.
	auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	auto parsed = h::parse_session_request(account, h::RequestKind::Next,
										   t::namespace_session_body(t::SessionResponsePurpose::Next, following,
																	 false, session_id(), 250ms));
	const auto &request = parsed->request();
	check(request.positions.size() == 3 && request.positions[0].cursor == d::NamespaceCursor{cluster, incarnation, 5} &&
		  !request.positions[1].cursor && !request.positions[2].cursor &&
		  request.all == d::DirectoryCursor{cluster, 3} && request.wait_ms == 250);
	for (auto bad : {t::Following{{{"b", {}}, {"a", {}}}, {}}, t::Following{{{"a b", {}}}, {}}, t::Following{}}) {
		bool refused = false;
		try {
			t::namespace_session_body(t::SessionResponsePurpose::Open, bad, false);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused);
	}
}
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc == 2);
		request_bodies();
		for (bool compressed : {false, true})
			run(argv[1], compressed);
		std::cout << "W5/W6 client against a test server passed\n";
	} catch (const std::exception &error) {
		std::cerr << "test_delivery_namespace_session: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
