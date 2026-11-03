// The classic distributor's owner (docs/design/http-coroutines.md D13, D10, D15):
//   T2  a commit held on the owner never delays a GET or the health route on the same (only) reactor
//   T6  begin_shutdown() answers a queued writer at once with the retry reply, an unclustered commit in
//       progress still returns its real result, and a clustered writer without a quorum gets the retry
//       reply within the grace instead of the 10 s backstop
//   T13 after D15's phases, forced with a grace of 0 while a clustered write waits on the owner, no task
//       frame is left
#include "detentd.hh"
#include "detent_http/http_client.hh"

#include <http_asio.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <thread>

using namespace std::chrono_literals;

namespace {
std::filesystem::path temp_path() {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("jk_owner_test_" + std::to_string(rng()) + ".json");
}
unsigned short temp_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(30000 + (rng() % 10000));
}
std::string snapshot(unsigned version) {
	return R"({"schema_version":1,"version":)" + std::to_string(version) + R"(,"knobs":{}})";
}
void wait_ready(unsigned short port) {
	for (auto deadline = std::chrono::steady_clock::now() + 5s;; std::this_thread::sleep_for(10ms)) {
		try {
			(void)detent_http::http_get("127.0.0.1", port, "/v1/health", 500ms);
			return;
		} catch (const std::exception &) {
			assert(std::chrono::steady_clock::now() < deadline);
		}
	}
}

// Holds the owner inside the commit hook while `hold` is set.
struct Latch {
	std::mutex mutex;
	std::condition_variable changed;
	bool hold = false;
	int entered = 0;
	void commit_begin() {
		std::unique_lock lock(mutex);
		++entered;
		changed.notify_all();
		changed.wait(lock, [&] { return !hold; });
	}
	void set(bool value) {
		{
			std::lock_guard lock(mutex);
			hold = value;
		}
		changed.notify_all();
	}
	bool wait_entered(int count) {
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, 5s, [&] { return entered >= count; });
	}
};

void test_reads_never_wait_on_a_commit() {
	Latch latch;
	detentd::Options options;
	options.persist_path = temp_path();
	options.on_commit_begin = [&] { latch.commit_begin(); };
	detentd::Distributor app(options);
	const auto port = temp_port();
	http::HttpAsioService service(app, 1, 256); // one reactor: a blocked loop would stall every route
	service.start(port);
	wait_ready(port);
	assert(detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(1), 2s).status == 200);
	latch.set(true);
	auto held = std::async(std::launch::async,
						   [&] { return detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(2), 20s); });
	assert(latch.wait_entered(2));
	// The commit is held on the owner: the reactor answers with the old version.
	const auto began = std::chrono::steady_clock::now();
	auto read = detent_http::http_get("127.0.0.1", port, "/v1/snapshot", 2s);
	auto health = detent_http::http_get("127.0.0.1", port, "/v1/health", 2s);
	assert(std::chrono::steady_clock::now() - began < 1s);
	assert(read.status == 200 && read.body.find("\"version\":1") != std::string::npos);
	assert(health.status == 200 && health.body.find("\"current_version\":1") != std::string::npos);
	latch.set(false);
	assert(held.get().status == 200);
	read = detent_http::http_get("127.0.0.1", port, "/v1/snapshot", 2s);
	assert(read.status == 200 && read.body.find("\"version\":2") != std::string::npos);
	app.begin_shutdown();
	service.stop();
	std::filesystem::remove(options.persist_path);
	std::puts("T2: a held commit does not delay GET or health OK");
}

void test_shutdown_answers_by_the_contract() {
	Latch latch;
	detentd::Options options;
	options.persist_path = temp_path();
	options.on_commit_begin = [&] { latch.commit_begin(); };
	detentd::Distributor app(options);
	const auto port = temp_port();
	http::HttpAsioService service(app, 1, 256);
	service.start(port);
	wait_ready(port);
	assert(detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(1), 2s).status == 200);
	latch.set(true);
	auto in_progress = std::async(std::launch::async,
								  [&] { return detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(2), 20s); });
	assert(latch.wait_entered(2));
	auto queued = std::async(std::launch::async,
							 [&] { return detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(3), 20s); });
	std::this_thread::sleep_for(300ms); // the second write is queued behind the held commit
	app.begin_shutdown();
	std::this_thread::sleep_for(100ms);
	latch.set(false);
	// A commit already started finishes and returns its real result; the writer behind it was never
	// applied and is told to retry.
	const auto first = in_progress.get();
	const auto second = queued.get();
	assert(first.status == 200 && first.body.find("\"accepted_version\":2") != std::string::npos);
	assert(second.status == 503 && second.body.find("retry") != std::string::npos);
	assert(app.current_version() == 2);
	service.stop();
	std::filesystem::remove(options.persist_path);
	std::puts("T6: begin_shutdown answers a commit in progress and a queued writer by the contract OK");
}

detentd::Options cluster_options(unsigned short port, unsigned short peer) {
	detentd::Options options;
	options.persist_path = temp_path();
	detentd::Options::Cluster cluster;
	static std::mt19937_64 rng{std::random_device{}()};
	const auto multicast = static_cast<unsigned short>(40000 + (rng() % 10000));
	cluster.token = "owner-test-" + std::to_string(multicast);
	cluster.multicast_port = multicast;
	cluster.multicast_group = "239.255.9.10";
	cluster.multicast_interface = "127.0.0.1";
	cluster.self_address = "127.0.0.1:" + std::to_string(port);
	cluster.peers.push_back("127.0.0.1:" + std::to_string(peer)); // never started: no quorum
	cluster.raft_config.heartbeat_timeout = 0.05;
	cluster.raft_config.election_init = 0.05;
	cluster.raft_config.election_min = 0.15;
	cluster.raft_config.election_max = 0.3;
	options.cluster = std::move(cluster);
	return options;
}

void test_clustered_writer_without_quorum() {
	// begin_shutdown() ends the wait: the retry reply arrives long before the 10 s backstop.
	const auto port = temp_port();
	auto options = cluster_options(port, static_cast<unsigned short>(port + 1));
	detentd::Distributor app(options);
	http::HttpAsioService service(app, 1, 256);
	service.start(port);
	wait_ready(port);
	auto waiting = std::async(std::launch::async,
							  [&] { return detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(1), 20s); });
	std::this_thread::sleep_for(500ms); // the write is on the owner, waiting for a quorum that never comes
	// T2, clustered: the health route on the same (only) reactor answers while the write waits.
	const auto reading = std::chrono::steady_clock::now();
	const auto health = detent_http::http_get("127.0.0.1", port, "/v1/health", 2s);
	assert(health.status == 200 && std::chrono::steady_clock::now() - reading < 1s);
	assert(waiting.wait_for(0s) == std::future_status::timeout);
	const auto began = std::chrono::steady_clock::now();
	app.begin_shutdown();
	const auto reply = waiting.get();
	assert(reply.status == 503 && reply.body.find("retry") != std::string::npos);
	assert(std::chrono::steady_clock::now() - began < 2s);
	service.stop();
	std::filesystem::remove(options.persist_path);
	std::puts("T6: a clustered writer without a quorum gets the retry reply at the stop OK");
}

void test_teardown_leaves_no_frames() {
#ifdef TASK_COUNT_FRAMES // Debug builds define it for every target, as the library's inline counters need
	const auto frames = task::detail::live_frames();
#endif
	{
		const auto port = temp_port();
		auto options = cluster_options(port, static_cast<unsigned short>(port + 1));
		detentd::Distributor app(options);
		http::HttpAsioService service(app, 1, 256);
		service.set_shutdown_grace(0ms); // a forced stop: the handler is still waiting on the owner
		service.start(port);
		wait_ready(port);
		auto waiting = std::async(std::launch::async, [&] {
			try {
				return detent_http::http_put("127.0.0.1", port, "/v1/snapshot", snapshot(1), 20s).status;
			} catch (const std::exception &) {
				return 0; // the forced stop closed the connection
			}
		});
		std::this_thread::sleep_for(500ms);
		service.stop(); // no begin_shutdown(): the owner wait outlives the loops
		app.owner().stop();
		service.close();
		app.owner().close();
		service.destroy_roots();
		app.owner().destroy_roots();
		(void)waiting.get();
		std::filesystem::remove(options.persist_path);
	}
#ifdef TASK_COUNT_FRAMES
	assert(task::detail::live_frames() == frames);
	assert(task::detail::orphaned_waiters() == 0);
#endif
	std::puts("T13: no task frame survives D15's phases with a write suspended on the owner OK");
}
} // namespace

int main() {
	test_reads_never_wait_on_a_commit();
	test_shutdown_answers_by_the_contract();
	test_clustered_writer_without_quorum();
	test_teardown_leaves_no_frames();
	return 0;
}
