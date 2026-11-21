// The agent coordinator on W5/W6 (D8): it follows its namespaces on one session and publishes each
// response's frames as one store cycle with an entry per namespace. A reader of each entry sees the
// server's state at the server's head through full copies, entries, blocks, a drop, a W7
// re-creation and a repair (D10), and a restarted agent is ready from its entries with the source
// down, then resumes from them.
#include "coordinator_agent_fixture.hh"
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
using coordinator_agent::Agent;
using coordinator_agent::matches;
using coordinator_agent::message;
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

			// A delta that does not apply to app's base asks a full copy (D10.3), so a wrong base is never
			// stuck failing every later delta.
			agent.fail_next_build(std::make_exception_ptr(std::invalid_argument("delta outside the base")));
			{
				std::lock_guard lock(server.mutex);
				server.find("app").transactions.push_back({{"app:k2", def(52, 0)}});
			}
			state = agent.until([&](const auto &s) { return at_heads(server, s, followed) && s.repair.completed == 2; },
								"failed delta repaired");
			check(state.repair.requested == 2 && state.failures);
			matches(server, root, limits, "app");

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
		d::NamespaceCursor published;
		{
			// D136: an entry whose stored bytes are damaged passes startup (C2 checks presence), fails
			// alone when its base recovers, and a fresh full copy replaces it. D139: app's repair names the
			// position app published, and a source whose app is older answers it below that position:
			// ignoring the floor, its copy is skipped; honoring it, it answers nothing.
			std::lock_guard lock(server.mutex);
			published = server.find("app").history().head();
			server.repair_head["app"] = server.find("app").transactions.size() - 1;
			server.ignore_floor = true;
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
			const auto skipped = agent.until([](const auto &s) { return s.repair.below > 0; }, "app's copy below its floor");
			const auto answered = server.answered.load();
			const auto older = agent.until([&](const auto &s) { return server.answered > answered + 2; }, "more answers");
			check(skipped.repair.pending && older.repair.pending && older.repair.completed <= 1); // web's only
			const auto idle = [&] {
				std::lock_guard lock(server.mutex);
				server.ignore_floor = false;
				check(!server.repairs.empty() && server.repairs.back() == published);
				return server.idle.load();
			}();
			agent.until([&](const auto &s) { return server.idle > idle + 1; }, "no answer below the floor");
			{
				std::lock_guard lock(server.mutex);
				server.repair_head.clear();
			}
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
