// namespace-s6 A3: the agent's client side speaks W5/W6 against a test server. The server parses W5
// bodies with the session request parser, filters by grants (W13), answers from a test history
// source per namespace, and sends every frame in one fragment-framed response.
#include "namespace_server_fixture.hh"
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

using namespace namespace_server;
namespace {
// The agent's store as the frames build it: per namespace, its cursor and records. A response's
// frames are prepared per namespace and published together, which bumps each namespace's change word.
struct Store final : t::FramePreparer {
	std::map<std::string, std::pair<d::NamespaceCursor, State>> namespaces;
	std::map<std::string, unsigned> change;
	std::optional<d::DirectoryCursor> directory_at;
	std::vector<d::DirectoryEntry> created;
	std::map<std::string, d::FrameKind> kinds; // the last frame kind per namespace
	unsigned prepared = 0;
	std::size_t inflate = 0; // Extra bytes each prepared frame reports.
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
		std::size_t bytes() const noexcept override { return 64 + 64 * state_.size() + store_.inflate; }
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
	// The prepared frames' own bytes are charged while staged: frames reporting 32 MiB overflow a 16 MiB ledger.
	{
		auto ledger = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
		auto limited = options;
		limited.limits.storage_account = ledger;
		store.inflate = 32 * 1024 * 1024;
		check(fails<d::StoragePressure>(
			io, scheduler, t::next_namespace_session(limited, session, store.following(true), 0ms, store)));
		store.inflate = 0;
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
	// DD3: open asks for page transfers, and for a refused namespace as records; next carries neither.
	following.pages = true;
	following.records = {"b", "c"};
	const auto open = t::namespace_session_body(t::SessionResponsePurpose::Open, following, false);
	check(open.ends_with(",\"all\":3,\"pages\":true,\"records\":[\"b\",\"c\"]}"));
	auto reopened = h::parse_session_request(account, h::RequestKind::Open, open);
	check(reopened->request().pages && reopened->request().records.size() == 2 &&
		  reopened->request().records[1] == "c");
	const auto next = t::namespace_session_body(t::SessionResponsePurpose::Next, following, false, session_id(), 250ms);
	check(next.ends_with(",\"wait_ms\":250}"));
	auto renext = h::parse_session_request(account, h::RequestKind::Next, next);
	check(!renext->request().pages && renext->request().records.empty() && !renext->request().resume);
	// D8: next asks a held transfer's rest; open never does.
	following.resume = t::Following::Resume{"a", {}, 3, d::transfer_chunk_bytes};
	following.resume->root.fill('\x0f');
	const auto resuming = t::namespace_session_body(t::SessionResponsePurpose::Next, following, false, session_id(), 250ms);
	std::string root_hex;
	for (int i = 0; i < 32; ++i)
		root_hex += "0f";
	check(resuming.ends_with(",\"resume\":[\"a\",\"" + root_hex + "\",3,65536]}"));
	auto resumed = h::parse_session_request(account, h::RequestKind::Next, resuming);
	check(resumed->request().resume && resumed->request().resume->name == "a" &&
		  resumed->request().resume->root == following.resume->root && resumed->request().resume->after == 3 &&
		  resumed->request().resume->offset == d::transfer_chunk_bytes);
	check(t::namespace_session_body(t::SessionResponsePurpose::Open, following, false).find("resume") == std::string::npos);
	following.resume.reset();
	following.records = {"c", "b"};
	bool unordered = false;
	try {
		t::namespace_session_body(t::SessionResponsePurpose::Open, following, false);
	} catch (const std::invalid_argument &) {
		unordered = true;
	}
	check(unordered);
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
