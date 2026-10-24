// Real multi-node cluster behavior: cluster::Raft (an already-tested port of
// Xapiand's own production leader-election + replicated-log consensus,
// vendored via the `cluster` dependency) elects exactly one leader among
// three real Distributor+HttpAsioService processes-in-one-binary, replicates
// PUT/PATCH writes to every member regardless of which node the HTTP request
// landed on, and re-elects a new leader (with writes continuing to work)
// when the current leader is stopped -- this is what replaces the OLD
// best-effort "announce version + pull from a peer" gossip, which had no
// concept of a quorum or a single writer at all (see the older git history
// / README's "Consistency caveat" this fixes, not just documents).
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "detentd.hh"
#include "detent_http/http_client.hh"

#include <http_asio.h>

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("jk_raft_" + std::to_string(rng()) + std::string(suffix));
}

auto &reserved_ports() {
	static std::map<unsigned short, std::unique_ptr<asio::ip::tcp::acceptor>> ports;
	return ports;
}

unsigned short temp_port() {
	static asio::io_context io;
	auto socket = std::make_unique<asio::ip::tcp::acceptor>(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
	const auto port = socket->local_endpoint().port();
	reserved_ports().emplace(port, std::move(socket));
	return port;
}

unsigned short temp_mcast_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(31000 + (rng() % 6000));
}

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(20ms);
	}
	return pred();
}

// Real timing (1s heartbeat, 10-30s election) would make a test suite
// glacially slow; tiny values (matching the vendored library's OWN demo/
// benchmark timings, see cluster's raft_election.cc) let an election
// resolve in well under a second while exercising the identical state
// machine and wire protocol.
cluster::RaftConfig test_raft_config() {
	cluster::RaftConfig cfg;
	cfg.heartbeat_timeout = 0.05;
	cfg.election_init = 0.05;
	cfg.election_min = 0.15;
	cfg.election_max = 0.3;
	return cfg;
}

struct RunningNode {
	detentd::Options options;
	std::unique_ptr<detentd::Distributor> app;
	std::unique_ptr<http::HttpAsioService> service;
	unsigned short port;

	explicit RunningNode(detentd::Options opts) : options(std::move(opts)),
		port(static_cast<unsigned short>(std::stoul(options.cluster->self_address.substr(
			options.cluster->self_address.rfind(':') + 1)))) {
		start();
	}

	void start() {
		app = std::make_unique<detentd::Distributor>(options);
		service = std::make_unique<http::HttpAsioService>(*app, 2, 4, 256);
		service->enable_conditional();
		reserved_ports().erase(port);
		service->start(port);
	}

	// Simulates a hard stop (crash or graceful shutdown, this test doesn't
	// distinguish) -- destroys the HTTP service and the Distributor (and,
	// with it, its Raft instance and Bus), freeing the port for restart.
	void stop() {
		if (service) service->stop();
		service.reset();
		app.reset();
	}

	~RunningNode() { stop(); }
};

std::string self_endpoint(unsigned short port) { return "127.0.0.1:" + std::to_string(port); }

int leader_count(const std::vector<std::unique_ptr<RunningNode>>& nodes) {
	int count = 0;
	for (const auto& n : nodes) {
		if (n->app && n->app->is_leader()) ++count;
	}
	return count;
}

} // namespace

int main() {
	constexpr int kN = 3;
	auto mcast_port = temp_mcast_port();
	std::string token = "jk-raft-test-" + std::to_string(mcast_port);
	std::string group = "239.255.9.9";

	std::vector<unsigned short> ports;
	for (int i = 0; i < kN; ++i) ports.push_back(temp_port());
	// Guarantee distinct ports (temp_port() draws from a wide range, but a
	// real collision -- however unlikely -- would make this test flaky, not
	// wrong, so make it impossible instead of merely improbable).
	for (int i = 0; i < kN; ++i)
		for (int j = i + 1; j < kN; ++j)
			if (ports[i] == ports[j]) ports[j] = static_cast<unsigned short>(ports[j] + 1);

	std::vector<std::filesystem::path> persist_paths;
	for (int i = 0; i < kN; ++i) persist_paths.push_back(temp_path("_n" + std::to_string(i) + ".json"));

	auto make_options = [&](int i) {
		detentd::Options opts;
		opts.persist_path = persist_paths[static_cast<std::size_t>(i)];
		detentd::Options::Cluster cluster;
		cluster.token = token;
		cluster.multicast_port = mcast_port;
		cluster.multicast_group = group;
		cluster.multicast_interface = "127.0.0.1";
		cluster.self_address = self_endpoint(ports[static_cast<std::size_t>(i)]);
		for (int j = 0; j < kN; ++j) {
			if (j != i) cluster.peers.push_back(self_endpoint(ports[static_cast<std::size_t>(j)]));
		}
		cluster.raft_config = test_raft_config();
		opts.cluster = std::move(cluster);
		return opts;
	};

	std::vector<std::unique_ptr<RunningNode>> nodes;
	for (int i = 0; i < kN; ++i) nodes.push_back(std::make_unique<RunningNode>(make_options(i)));
	std::this_thread::sleep_for(100ms); // let the reactor threads bind before the election clock starts mattering

	// Exactly one leader elected within a bounded time -- real consensus,
	// not assumed: every node's own is_leader() (driven by Raft's
	// set_leader() callback) must agree there is exactly one.
	bool elected = wait_until([&] { return leader_count(nodes) == 1; }, 5s);
	assert(elected);
	std::puts("3-node cluster: exactly one leader elected OK");

	// A write via ANY node -- deliberately including a non-leader, to prove
	// Raft's own add_command() forwarding (not a reject-and-redirect this
	// code never implements) is what makes this work -- replicates to
	// every member.
	int follower_idx = -1;
	for (int i = 0; i < kN; ++i) {
		if (!nodes[static_cast<std::size_t>(i)]->app->is_leader()) { follower_idx = i; break; }
	}
	assert(follower_idx >= 0);

	auto put = detent_http::http_put(
		"127.0.0.1", nodes[static_cast<std::size_t>(follower_idx)]->port, "/v1/snapshot",
		R"({"schema_version":1,"version":1,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
	assert(put.status == 200);

	for (int i = 0; i < kN; ++i) {
		bool replicated = wait_until([&] { return nodes[static_cast<std::size_t>(i)]->app->current_version() == 1; }, 3s);
		assert(replicated);
	}
	std::puts("write via a follower replicates to every member (Raft forwarding) OK");

	// PATCH goes through the exact same path.
	auto patch = detent_http::http_patch(
		"127.0.0.1", nodes[static_cast<std::size_t>(follower_idx)]->port, "/v1/snapshot",
		R"({"schema_version":1,"version":2,"knobs":{"team:flag":{"type":"bool","default":false}}})", 5s);
	assert(patch.status == 200);
	for (int i = 0; i < kN; ++i) {
		bool replicated = wait_until([&] { return nodes[static_cast<std::size_t>(i)]->app->current_version() == 2; }, 3s);
		assert(replicated);
	}
	auto get_after_patch = detent_http::http_get(
		"127.0.0.1", nodes[static_cast<std::size_t>(follower_idx)]->port, "/v1/snapshot", 2s);
	assert(get_after_patch.body.find("\"default\":false") != std::string::npos);
	std::puts("PATCH via a follower replicates to every member OK");

	// Failover: stop the current leader; the remaining two (a live
	// majority: 2 of 3) must elect a NEW leader and keep accepting writes.
	int leader_idx = -1;
	for (int i = 0; i < kN; ++i) {
		if (nodes[static_cast<std::size_t>(i)]->app->is_leader()) { leader_idx = i; break; }
	}
	assert(leader_idx >= 0);
	auto stopped_leader_address = self_endpoint(ports[static_cast<std::size_t>(leader_idx)]);
	nodes[static_cast<std::size_t>(leader_idx)]->stop();

	std::vector<int> survivors;
	for (int i = 0; i < kN; ++i) if (i != leader_idx) survivors.push_back(i);

	bool refailover = wait_until(
		[&] {
			int count = 0;
			for (int i : survivors) {
				if (nodes[static_cast<std::size_t>(i)]->app->is_leader()) ++count;
			}
			return count == 1;
		},
		5s);
	assert(refailover);
	int new_leader_idx = -1;
	for (int i : survivors) {
		if (nodes[static_cast<std::size_t>(i)]->app->is_leader()) new_leader_idx = i;
	}
	assert(new_leader_idx >= 0);
	assert(self_endpoint(ports[static_cast<std::size_t>(new_leader_idx)]) != stopped_leader_address);
	std::puts("stopping the leader triggers a new election among the surviving majority OK");

	auto put_after_failover = detent_http::http_put(
		"127.0.0.1", nodes[static_cast<std::size_t>(survivors[0])]->port, "/v1/snapshot",
		R"({"schema_version":1,"version":3,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
	assert(put_after_failover.status == 200);
	for (int i : survivors) {
		bool replicated = wait_until([&] { return nodes[static_cast<std::size_t>(i)]->app->current_version() == 3; }, 3s);
		assert(replicated);
	}
	std::puts("writes continue to work after failover, with the new leader OK");

	// Restart-safety: the stopped node (which crash-restarts here, not just
	// stays down) must not be immediately eligible to vote or be elected --
	// this is the whole mechanism DetentRaftDelegate::active() implements
	// to avoid persisting current_term_/voted_for_ across a restart (see
	// its own comment, raft_delegate.hh). Right after restart it must
	// report is_leader()==false; the cluster must keep working through the
	// surviving members regardless; and once the startup_silence window
	// elapses, the restarted node must actually rejoin (participate in a
	// future election / apply future writes), not stay permanently mute.
	nodes[static_cast<std::size_t>(leader_idx)]->start(); // same port, fresh Distributor+Raft -- a real restart
	assert(!nodes[static_cast<std::size_t>(leader_idx)]->app->is_leader());
	std::puts("a just-restarted node is never immediately (re-)elected leader OK");

	auto put_during_silence = detent_http::http_put(
		"127.0.0.1", nodes[static_cast<std::size_t>(survivors[0])]->port, "/v1/snapshot",
		R"({"schema_version":1,"version":4,"knobs":{"team:flag":{"type":"bool","default":false}}})", 5s);
	assert(put_during_silence.status == 200);
	std::puts("the cluster keeps accepting writes while the restarted node is still silent OK");

	// test_raft_config()'s election_max is 0.3s, so 3x is 0.9s of silence;
	// give it real margin (well more than the window itself) before
	// declaring "it never rejoined" a failure.
	bool rejoined = wait_until(
		[&] { return nodes[static_cast<std::size_t>(leader_idx)]->app->current_version() == 4; }, 5s);
	assert(rejoined);
	std::puts("the restarted node rejoins and catches up once its silence window elapses OK");

	for (auto& p : persist_paths) std::filesystem::remove(p);

	// A fresh bootstrap (no marker files) must elect a leader within one
	// normal election cycle, not 3x election_max -- election_max=1.5s
	// here specifically so that bound (4.5s) is distinguishable from
	// normal election timing within the test's own assertion below.
	{
		constexpr int kN2 = 3;
		auto mcast_port2 = temp_mcast_port();
		std::string token2 = "jk-raft-bootstrap-test-" + std::to_string(mcast_port2);
		std::string group2 = "239.255.9.10";

		std::vector<unsigned short> ports2;
		for (int i = 0; i < kN2; ++i) ports2.push_back(temp_port());
		for (int i = 0; i < kN2; ++i)
			for (int j = i + 1; j < kN2; ++j)
				if (ports2[i] == ports2[j]) ports2[j] = static_cast<unsigned short>(ports2[j] + 1);

		std::vector<std::filesystem::path> persist_paths2;
		for (int i = 0; i < kN2; ++i) persist_paths2.push_back(temp_path("_boot_n" + std::to_string(i) + ".json"));
		// No marker files exist yet for these fresh persist paths -- this
		// is what makes it a genuine first-ever launch, not a restart.
		for (auto& p : persist_paths2) assert(!std::filesystem::exists(p.string() + ".cluster_marker"));

		cluster::RaftConfig bootstrap_config;
		bootstrap_config.heartbeat_timeout = 0.2;
		bootstrap_config.election_init = 0.2;
		bootstrap_config.election_min = 0.5;
		bootstrap_config.election_max = 1.5;

		std::vector<std::unique_ptr<RunningNode>> nodes2;
		for (int i = 0; i < kN2; ++i) {
			detentd::Options opts;
			opts.persist_path = persist_paths2[static_cast<std::size_t>(i)];
			detentd::Options::Cluster cl;
			cl.token = token2;
			cl.multicast_port = mcast_port2;
			cl.multicast_group = group2;
			cl.multicast_interface = "127.0.0.1";
			cl.self_address = self_endpoint(ports2[static_cast<std::size_t>(i)]);
			for (int j = 0; j < kN2; ++j) {
				if (j != i) cl.peers.push_back(self_endpoint(ports2[static_cast<std::size_t>(j)]));
			}
			cl.raft_config = bootstrap_config;
			opts.cluster = std::move(cl);
			nodes2.push_back(std::make_unique<RunningNode>(std::move(opts)));
		}
		std::this_thread::sleep_for(100ms);

		auto start = std::chrono::steady_clock::now();
		bool elected2 = wait_until([&] { return leader_count(nodes2) == 1; }, 3s);
		auto elapsed = std::chrono::steady_clock::now() - start;
		assert(elected2);
		// 3x election_max would be 4.5s; a correct fresh bootstrap elects
		// well under that, within roughly one normal election cycle.
		assert(elapsed < 3s);
		std::puts("a fresh cluster bootstrap elects a leader without waiting out the restart-safety window OK");

		for (auto& p : persist_paths2) {
			std::filesystem::remove(p);
			std::filesystem::remove(p.string() + ".cluster_marker");
		}
	}

	// A permanently-gone FOLLOWER (not the leader -- failover already
	// covers that case) must not block the surviving majority from
	// committing new writes. is_alive() has to actually reflect silence,
	// not just static configured membership, for this to work.
	{
		constexpr int kN3 = 3;
		auto mcast_port3 = temp_mcast_port();
		std::string token3 = "jk-raft-liveness-test-" + std::to_string(mcast_port3);
		std::string group3 = "239.255.9.11";

		std::vector<unsigned short> ports3;
		for (int i = 0; i < kN3; ++i) ports3.push_back(temp_port());
		for (int i = 0; i < kN3; ++i)
			for (int j = i + 1; j < kN3; ++j)
				if (ports3[i] == ports3[j]) ports3[j] = static_cast<unsigned short>(ports3[j] + 1);

		std::vector<std::filesystem::path> persist_paths3;
		for (int i = 0; i < kN3; ++i) persist_paths3.push_back(temp_path("_live_n" + std::to_string(i) + ".json"));

		auto make_options3 = [&](int i) {
			detentd::Options opts;
			opts.persist_path = persist_paths3[static_cast<std::size_t>(i)];
			detentd::Options::Cluster cl;
			cl.token = token3;
			cl.multicast_port = mcast_port3;
			cl.multicast_group = group3;
			cl.multicast_interface = "127.0.0.1";
			cl.self_address = self_endpoint(ports3[static_cast<std::size_t>(i)]);
			for (int j = 0; j < kN3; ++j) {
				if (j != i) cl.peers.push_back(self_endpoint(ports3[static_cast<std::size_t>(j)]));
			}
			cl.raft_config = test_raft_config();
			opts.cluster = std::move(cl);
			return opts;
		};

		std::vector<std::unique_ptr<RunningNode>> nodes3;
		for (int i = 0; i < kN3; ++i) nodes3.push_back(std::make_unique<RunningNode>(make_options3(i)));
		std::this_thread::sleep_for(100ms);

		bool elected3 = wait_until([&] { return leader_count(nodes3) == 1; }, 5s);
		assert(elected3);
		int leader_idx3 = -1;
		for (int i = 0; i < kN3; ++i) {
			if (nodes3[static_cast<std::size_t>(i)]->app->is_leader()) { leader_idx3 = i; break; }
		}
		assert(leader_idx3 >= 0);

		auto put1 = detent_http::http_put(
			"127.0.0.1", nodes3[static_cast<std::size_t>(leader_idx3)]->port, "/v1/snapshot",
			R"({"schema_version":1,"version":1,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
		assert(put1.status == 200);

		int dead_follower_idx = -1;
		for (int i = 0; i < kN3; ++i) {
			if (i != leader_idx3) { dead_follower_idx = i; break; }
		}
		assert(dead_follower_idx >= 0);
		nodes3[static_cast<std::size_t>(dead_follower_idx)]->stop(); // gone for good, this scenario never restarts it

		// Several more writes, each with its own generous timeout, must all
		// eventually succeed through the 2-of-3 surviving majority.
		for (int version = 2; version <= 5; ++version) {
			auto put = detent_http::http_put(
				"127.0.0.1", nodes3[static_cast<std::size_t>(leader_idx3)]->port, "/v1/snapshot",
				R"({"schema_version":1,"version":)" + std::to_string(version) +
					R"(,"knobs":{"team:flag":{"type":"bool","default":true}}})",
				5s);
			assert(put.status == 200);
		}
		std::puts("a permanently-gone follower does not block the surviving majority from committing writes OK");

		for (auto& p : persist_paths3) {
			std::filesystem::remove(p);
			std::filesystem::remove(p.string() + ".cluster_marker");
		}
	}

	// A node that originates a write, crashes, and restarts gets a fresh
	// command_counter_ starting back at 0 -- its correlation ids would
	// collide with ones from before the crash if nothing else made them
	// unique. A collision lets a REPLAYED old log entry (delivered during
	// catch-up) steal the pending_commands_ slot a brand new write just
	// registered under the same id, handing that write's waiter the old
	// entry's (wrong) result instead of its own.
	{
		constexpr int kN4 = 3;
		auto mcast_port4 = temp_mcast_port();
		std::string token4 = "jk-raft-corrid-test-" + std::to_string(mcast_port4);
		std::string group4 = "239.255.9.12";

		std::vector<unsigned short> ports4;
		for (int i = 0; i < kN4; ++i) ports4.push_back(temp_port());
		for (int i = 0; i < kN4; ++i)
			for (int j = i + 1; j < kN4; ++j)
				if (ports4[i] == ports4[j]) ports4[j] = static_cast<unsigned short>(ports4[j] + 1);

		std::vector<std::filesystem::path> persist_paths4;
		for (int i = 0; i < kN4; ++i) persist_paths4.push_back(temp_path("_corrid_n" + std::to_string(i) + ".json"));

		auto make_options4 = [&](int i) {
			detentd::Options opts;
			opts.persist_path = persist_paths4[static_cast<std::size_t>(i)];
			detentd::Options::Cluster cl;
			cl.token = token4;
			cl.multicast_port = mcast_port4;
			cl.multicast_group = group4;
			cl.multicast_interface = "127.0.0.1";
			cl.self_address = self_endpoint(ports4[static_cast<std::size_t>(i)]);
			for (int j = 0; j < kN4; ++j) {
				if (j != i) cl.peers.push_back(self_endpoint(ports4[static_cast<std::size_t>(j)]));
			}
			cl.raft_config = test_raft_config();
			opts.cluster = std::move(cl);
			return opts;
		};

		std::vector<std::unique_ptr<RunningNode>> nodes4;
		for (int i = 0; i < kN4; ++i) nodes4.push_back(std::make_unique<RunningNode>(make_options4(i)));
		std::this_thread::sleep_for(100ms);

		bool elected4 = wait_until([&] { return leader_count(nodes4) == 1; }, 5s);
		assert(elected4);
		int follower_idx4 = -1;
		for (int i = 0; i < kN4; ++i) {
			if (!nodes4[static_cast<std::size_t>(i)]->app->is_leader()) { follower_idx4 = i; break; }
		}
		assert(follower_idx4 >= 0);

		// This follower originates a write (its own command_counter_
		// reaches 1), giving it a corr id ("addr#...#0") that will still
		// be sitting in everyone's log when it later restarts.
		auto put1 = detent_http::http_put(
			"127.0.0.1", nodes4[static_cast<std::size_t>(follower_idx4)]->port, "/v1/snapshot",
			R"({"schema_version":1,"version":1,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
		assert(put1.status == 200);
		for (int i = 0; i < kN4; ++i) {
			bool replicated = wait_until([&] { return nodes4[static_cast<std::size_t>(i)]->app->current_version() == 1; }, 3s);
			assert(replicated);
		}

		nodes4[static_cast<std::size_t>(follower_idx4)]->stop(); // crash: command_counter_/pending slots are gone

		// A write while it's down, so its own log is behind and it has
		// something real to replay once it rejoins.
		int other_idx4 = -1;
		for (int i = 0; i < kN4; ++i) {
			if (i != follower_idx4) { other_idx4 = i; break; }
		}
		assert(other_idx4 >= 0);
		auto put2 = detent_http::http_put(
			"127.0.0.1", nodes4[static_cast<std::size_t>(other_idx4)]->port, "/v1/snapshot",
			R"({"schema_version":1,"version":2,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
		assert(put2.status == 200);

		nodes4[static_cast<std::size_t>(follower_idx4)]->start(); // fresh Distributor -- command_counter_ back at 0
		std::this_thread::sleep_for(100ms); // let the reactor thread bind before hitting it directly over HTTP

		// Not yet caught up on the backlog, this SAME node originates a
		// brand new write -- its fresh command_counter_ generates the
		// exact same "addr#...#0" shape the pre-crash write used. Without
		// session_id_ making every restart's ids unique, the pre-crash
		// entry's replay (still working through the backlog) could
		// resolve THIS request's wait with the wrong (stale) result.
		auto put3 = detent_http::http_put(
			"127.0.0.1", nodes4[static_cast<std::size_t>(follower_idx4)]->port, "/v1/snapshot",
			R"({"schema_version":1,"version":3,"knobs":{"team:flag":{"type":"bool","default":true}}})", 5s);
		assert(put3.status == 200);
		assert(put3.body.find("\"accepted_version\":3") != std::string::npos);
		for (int i = 0; i < kN4; ++i) {
			bool replicated = wait_until([&] { return nodes4[static_cast<std::size_t>(i)]->app->current_version() == 3; }, 3s);
			assert(replicated);
		}
		std::puts("a node's own write right after its restart is not confused with one of its pre-crash entries OK");

		for (auto& p : persist_paths4) {
			std::filesystem::remove(p);
			std::filesystem::remove(p.string() + ".cluster_marker");
		}
	}

	return 0;
}
