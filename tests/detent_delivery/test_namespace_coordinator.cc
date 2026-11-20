// The agent coordinator on W5/W6 (D8): it follows its namespaces on one session and publishes each
// response's frames as one store cycle with an entry per namespace. A reader of each entry sees the
// server's state at the server's head through full copies, entries, blocks, a drop, a W7
// re-creation and a repair (D10), and a restarted agent is ready from its entries with the source
// down, then resumes from them.
#include "../../src/detent_delivery/coordinator_internal.hh"
#include "namespace_server_fixture.hh"
#include "detent/owner_loop.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/reader.hh"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <unistd.h>

namespace t = detent::delivery;
namespace l = detent::local;
using namespace namespace_server;

namespace {
std::string message(const std::exception_ptr &error) {
	try {
		if (error)
			std::rethrow_exception(error);
	} catch (const std::exception &failure) {
		return failure.what();
	} catch (...) {
	}
	return "unknown";
}
struct Agent {
	detent::OwnerLoop owner;
	std::shared_ptr<l::DiskStore> store;
	std::shared_ptr<t::NativeDriver> driver;
	std::shared_ptr<t::Coordinator> coordinator;
	asio::thread_pool worker{1};
	Agent(const std::filesystem::path &root, l::StoreOpenMode mode, t::CoordinatorOptions options,
		  std::shared_ptr<d::StorageLedger> account, d::RetirementDomain &domain,
		  std::shared_ptr<kronuz::io::completion::NativeQueue> queue) {
		l::DiskStoreOptions disk;
		disk.mode = mode;
		disk.limits = {64 * 1024 * 1024, 100, 10};
		disk.recovery_limits = options.view_limits;
		store = l::make_disk_store(domain, root, account, disk);
		driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account), owner.scheduler(),
													   std::move(queue));
		options.source.scheduler = &owner.scheduler();
		coordinator = t::make_coordinator(std::move(options), store, driver, domain, worker);
		asio::post(owner.io(), [this] { task::spawn(task::scheduler_ref::of(owner.scheduler()), coordinator->run()); });
	}
	void repair(std::string_view name) {
		std::promise<void> done;
		asio::post(owner.io(), [&] {
			t::detail::CoordinatorAccess::request_repair(*coordinator, name);
			done.set_value();
		});
		done.get_future().get();
	}
	t::CoordinatorStatus status() {
		std::promise<t::CoordinatorStatus> out;
		asio::post(owner.io(), [&] { out.set_value(coordinator->status()); });
		return out.get_future().get();
	}
	template <class Done> t::CoordinatorStatus until(Done done, const char *what) {
		for (const auto limit = std::chrono::steady_clock::now() + 30s;; std::this_thread::sleep_for(5ms)) {
			auto state = status();
			if (state.phase == t::CoordinatorPhase::Fenced)
				throw std::runtime_error(std::string("coordinator fenced waiting for ") + what + ": " + message(state.error));
			if (done(state))
				return state;
			if (std::chrono::steady_clock::now() > limit) {
				std::string at;
				for (const auto &each : state.namespaces)
					at += " " + each.name + "@" + (each.cursor ? std::to_string(each.cursor->position) : "-");
				throw std::runtime_error(std::string("timed out waiting for ") + what + ": phase " +
										 std::to_string(int(state.phase)) + ", ready " + std::to_string(state.ready) +
										 ", responses " + std::to_string(state.responses) + ", failures " +
										 std::to_string(state.failures) + ", namespaces" + at + ", error " +
										 message(state.error));
			}
		}
	}
	~Agent() {
		asio::post(owner.io(), [this] { coordinator->close(); });
		for (const auto limit = std::chrono::steady_clock::now() + 30s; std::chrono::steady_clock::now() < limit;
			 std::this_thread::sleep_for(5ms)) {
			std::promise<bool> drained;
			asio::post(owner.io(), [&] { drained.set_value(coordinator->drained()); });
			if (drained.get_future().get())
				break;
		}
		owner.stop();
		owner.close();
		owner.destroy_roots();
		worker.join();
		coordinator.reset();
		driver.reset();
		store.reset();
	}
};

// Every followed namespace's cursor at the agent is the server's head.
bool at_heads(Server &server, const t::CoordinatorStatus &state, const std::vector<std::string> &followed) {
	std::lock_guard lock(server.mutex);
	for (const auto &name : followed) {
		const auto &served = server.find(name);
		const auto found = std::find_if(state.namespaces.begin(), state.namespaces.end(),
										[&](const auto &each) { return each.name == name; });
		if (served.dropped) {
			if (found != state.namespaces.end() && found->cursor)
				return false;
			continue;
		}
		if (found == state.namespaces.end() || found->cursor != served.history().head())
			return false;
	}
	return state.ready;
}

// A reader attached to the namespace's entry by name sees the server's state at its head.
void matches(Server &server, const std::filesystem::path &root, const l::ViewLimits &limits, const std::string &name) {
	State expected;
	std::uint64_t head = 0;
	{
		std::lock_guard lock(server.mutex);
		const auto history = server.find(name).history();
		head = history.head().position;
		expected = history.state_at(head);
	}
	l::GenerationReader reader(root, limits, {}, {}, name);
	reader.refresh();
	const auto view = reader.read_view();
	check(view.cursor().position == head);
	std::uint32_t live = 0;
	for (const auto &[key, held] : expected) {
		const auto bare = std::string_view(key).substr(name.size() + 1);
		check(view.contains(name, bare) == held.value.has_value());
		if (!held.value)
			continue;
		++live;
		if (!held.value->rollout_pct && held.value->overrides.empty())
			check(view.get(name, bare).value() == held.value->default_value);
	}
	check(view.size() == live);
}
bool unavailable(const std::filesystem::path &root, const l::ViewLimits &limits, const std::string &name) {
	try {
		l::GenerationReader reader(root, limits, {}, {}, name);
		reader.refresh();
	} catch (const l::GenerationUnavailable &) {
		return true;
	}
	return false;
}
} // namespace

int main(int argc, char **argv) {
	const auto root = std::filesystem::current_path() / ".scratch" / ("namespace-coordinator-" + std::to_string(::getpid()));
	try {
		check(argc == 2);
		const std::filesystem::path certificates(argv[1]);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*server_tls,
								{(certificates / "one.pem").string(), (certificates / "one.key").string(),
								 (certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
		Server server(server_tls, {"app", "web", "secret"});
		server.served = {{"app", id('a'), 1, writes("app", 6, 1)},
						 {"secret", id('s'), 2, writes("secret", 3, 2)},
						 {"web", id('w'), 3, writes("web", 9, 3)}};

		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 256);
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);

		t::CoordinatorOptions options;
		options.namespaces = {"app", "web"};
		options.directory = root;
		options.view_limits = limits;
		options.retry_interval = 5ms;
		options.reconnect_maximum = 50ms;
		options.reconnect_seed = 7;
		options.cluster.fill('\x01');
		options.source.endpoint = server.acceptor.local_endpoint();
		options.source.hostname = "localhost";
		options.source.token = "0123456789abcdef0123456789abcdef";
		options.source.tls_context = client_tls;
		options.source.timeout = 5s;
		options.source.compression = server.compressed;
		const std::vector<std::string> followed{"app", "web"};
		{
			Agent agent(root, l::StoreOpenMode::FreshEmpty, options, account, domain, queue);
			// A cold start: one full copy per followed namespace, one store entry each; secret is not followed.
			auto state = agent.until([&](const auto &s) { return at_heads(server, s, followed); }, "first heads");
			check(state.selected.namespaces.size() == 2 && !state.selected.find("secret"));
			matches(server, root, limits, "app");
			matches(server, root, limits, "web");

			// app moves by ten (a blocks answer) and web by one (an entry): both reach the new heads.
			{
				std::lock_guard lock(server.mutex);
				for (auto &tx : writes("app", 10, 5))
					server.find("app").transactions.push_back(std::move(tx));
				server.find("web").transactions.push_back({{"web:k9", def(9, 0)}});
			}
			agent.until([&](const auto &s) { return at_heads(server, s, followed); }, "moved heads");
			matches(server, root, limits, "app");
			matches(server, root, limits, "web");

			// W7: app is re-created under a new incarnation, its positions above the old ones (D1); a full
			// copy replaces its entry.
			{
				std::lock_guard lock(server.mutex);
				auto &app = server.find("app");
				app.incarnation = id('A');
				app.transactions = writes("app", 20, 8);
			}
			state = agent.until([&](const auto &s) { return at_heads(server, s, followed); }, "re-created app");
			matches(server, root, limits, "app");

			// A repair refetches app as a full copy at its head (D10); web keeps its generation.
			const auto web_before = state.selected.find("web")->generation;
			agent.repair("app");
			state = agent.until([](const auto &s) { return s.repair.completed == 1 && !s.repair.pending; }, "repair");
			check(state.repair.requested == 1);
			check(state.selected.find("web")->generation == web_before);
			matches(server, root, limits, "app");
			matches(server, root, limits, "web");

			// web is dropped: its entry leaves the table, app keeps serving.
			{
				std::lock_guard lock(server.mutex);
				auto &web = server.find("web");
				web.dropped = web.history().head();
			}
			state = agent.until([&](const auto &s) { return !s.selected.find("web"); }, "dropped web");
			check(unavailable(root, limits, "web"));
			matches(server, root, limits, "app");
		}
		{
			// With the source down, a restart is ready from its entries as they stand (C4).
			std::lock_guard lock(server.mutex);
			server.behind = true;
		}
		{
			options.namespaces = {"app"};
			Agent agent(root, l::StoreOpenMode::ExistingManaged, options, account, domain, queue);
			const auto state = agent.until([](const auto &s) { return s.ready && s.failures; }, "offline ready");
			check(state.selected.find("app") && !state.selected.find("web"));
			matches(server, root, limits, "app");
			options.namespaces = {"app", "web"};
		}
		{
			// A restart resumes from its entries: app's next answer is current, so no frame is sent.
			std::lock_guard lock(server.mutex);
			server.behind = false;
			server.find("web").dropped.reset();
			server.answered = 0;
		}
		{
			Agent agent(root, l::StoreOpenMode::ExistingManaged, options, account, domain, queue);
			agent.until([&](const auto &s) { return at_heads(server, s, followed); }, "restarted heads");
			matches(server, root, limits, "app");
			matches(server, root, limits, "web");
			check(server.answered == 1); // web's full copy only
		}
		{
			// D136: an entry whose stored bytes are damaged passes startup (C2 checks presence), fails
			// alone when its base recovers, and a fresh full copy replaces it.
			std::lock_guard lock(server.mutex);
			server.find("app").transactions.push_back({{"app:k1", def(41, 0)}});
			server.find("web").transactions.push_back({{"web:k2", def(42, 0)}});
		}
		std::size_t damaged = 0;
		for (const auto &file : std::filesystem::directory_iterator(root))
			if (file.path().extension() == ".owner") {
				std::fstream bytes(file.path(), std::ios::in | std::ios::out | std::ios::binary);
				bytes.seekg(20);
				const char was = static_cast<char>(bytes.get());
				bytes.seekp(20);
				bytes.put(static_cast<char>(was ^ 0x5a));
				++damaged;
			}
		check(damaged > 0);
		{
			Agent agent(root, l::StoreOpenMode::ExistingManaged, options, account, domain, queue);
			const auto state = agent.until([&](const auto &s) { return at_heads(server, s, followed) && !s.repair.pending; },
										   "damaged entries replaced");
			check(state.repair.requested == 2 && state.repair.completed == 2);
			matches(server, root, limits, "app");
			matches(server, root, limits, "web");
		}
		check(!server.failure);
		queue.reset();
		domain.close().wait();
		runtime.shutdown();
		std::filesystem::remove_all(root);
		std::cout << "the coordinator follows its namespaces as one store entry each\n";
	} catch (const std::exception &error) {
		std::cerr << "test_namespace_coordinator: " << error.what() << '\n';
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		return 1;
	}
	return 0;
}
