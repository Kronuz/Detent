// detentd_fanout_bench -- the distributor's fan-out capacity: one Distributor
// and a configurable number of simulated polling watchers (raw conditional-
// GET loops, matching what detent_http::make_http_poll_source does),
// publishes a version bump mid-run, and reports: requests served, the
// 304-vs-200 ratio (the bandwidth-savings the ETag short-circuit buys), and
// fan-out latency -- wall-clock time from PUT-accepted to when each watcher
// first observed the new version. Real numbers, printed, not claimed.
//
// This is the same measurement as tests/detentd/test_load_fanout.cc (which
// stays small and ctest-registered, for fast CI regression coverage of the
// same code path) but defaults to a scale worth actually reporting.
//
//   cmake --build build && ./build/detentd_fanout_bench [watchers] [duration-ms] [poll-interval-ms] [reactors]
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include "detentd.hh"
#include "detent_http/http_client.hh"

#include <http_asio.h>

using namespace std::chrono_literals;

namespace {

unsigned short temp_port() {
	static std::mt19937_64 rng{std::random_device{}()};
	return static_cast<unsigned short>(24000 + (rng() % 8000));
}

// See test_distributor_client_integration.cc's identical helper: a fixed
// sleep_for(50ms) is not a portable "the reactor has bound" bound under
// scheduler contention, and the unguarded seed http_put() right after it
// would otherwise throw out of main() uncaught on an occasional slow start.
void wait_for_server_ready(unsigned short port, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	for (;;) {
		try {
			(void)detent_http::http_get("127.0.0.1", port, "/v1/health", 500ms);
			return;
		} catch (const std::exception&) {
			if (std::chrono::steady_clock::now() >= deadline) throw;
			std::this_thread::sleep_for(10ms);
		}
	}
}

} // namespace

int main(int argc, char** argv) {
	// Bigger defaults than the ctest-registered version: this binary isn't
	// gated by CI turnaround time, so its default run should be a number
	// worth reporting, not just a smoke test.
	int num_watchers = argc > 1 ? std::atoi(argv[1]) : 1000;
	auto run_duration = argc > 2 ? std::chrono::milliseconds{std::atoi(argv[2])} : 5000ms;
	auto poll_interval = argc > 3 ? std::chrono::milliseconds{std::atoi(argv[3])} : 200ms;
	int reactors = argc > 4 ? std::atoi(argv[4]) : 4;

	auto persist = std::filesystem::temp_directory_path() / "detentd_fanout_bench.json";
	std::filesystem::remove(persist);
	detentd::Options opts;
	opts.persist_path = persist;
	detentd::Distributor app(opts);
	// A worker pool sized well above num_watchers would defeat the point of
	// this test (PUT is the only offloaded route, and it is rare); a modest
	// pool is enough since GETs never touch it (should_offload()==false).
	http::HttpAsioService service(app, static_cast<std::size_t>(reactors), 8, 4096);
	service.enable_conditional();
	auto port = temp_port();
	service.start(port);
	std::this_thread::sleep_for(50ms);
	wait_for_server_ready(port, 3s);

	// Seed version 1 so watchers have something to converge FROM before the
	// mid-run publish to version 2.
	auto seed = detent_http::http_put("127.0.0.1", port, "/v1/snapshot",
	                                      R"({"schema_version":1,"version":1,"knobs":{}})", 2s);
	assert(seed.status == 200);

	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> total_requests{0};
	std::atomic<std::uint64_t> total_304{0};
	std::atomic<std::uint64_t> total_200{0};
	std::atomic<int> converged_count{0};
	std::vector<std::chrono::steady_clock::time_point> first_seen_v2(static_cast<std::size_t>(num_watchers));

	std::chrono::steady_clock::time_point publish_time;

	std::vector<std::thread> watchers;
	watchers.reserve(static_cast<std::size_t>(num_watchers));
	for (int i = 0; i < num_watchers; ++i) {
		watchers.emplace_back([&, i] {
			detent_http::HttpConnection conn("127.0.0.1", port, 2s);
			std::optional<std::string> etag;
			while (!stop.load(std::memory_order_relaxed)) {
				try {
					auto resp = conn.get("/v1/snapshot", etag);
					total_requests.fetch_add(1, std::memory_order_relaxed);
					if (resp.status == 304) {
						total_304.fetch_add(1, std::memory_order_relaxed);
					} else if (resp.status == 200) {
						total_200.fetch_add(1, std::memory_order_relaxed);
						etag = resp.etag;
						if (resp.body.find("\"version\":2") != std::string::npos &&
						    first_seen_v2[static_cast<std::size_t>(i)].time_since_epoch().count() == 0) {
							first_seen_v2[static_cast<std::size_t>(i)] = std::chrono::steady_clock::now();
							converged_count.fetch_add(1, std::memory_order_relaxed);
						}
					}
				} catch (const std::exception&) {
					// transient; a real client would back off, this is a stress probe
				}
				std::this_thread::sleep_for(poll_interval);
			}
		});
	}

	std::this_thread::sleep_for(run_duration / 2);
	publish_time = std::chrono::steady_clock::now();
	auto put2 = detent_http::http_put("127.0.0.1", port, "/v1/snapshot",
	                                      R"({"schema_version":1,"version":2,"knobs":{}})", 2s);
	assert(put2.status == 200);

	std::this_thread::sleep_for(run_duration / 2 + 500ms); // grace period for the last watchers to converge

	stop.store(true, std::memory_order_relaxed);
	for (auto& t : watchers) t.join();
	service.stop();
	std::filesystem::remove(persist);

	std::vector<double> latencies_ms;
	for (const auto& tp : first_seen_v2) {
		if (tp.time_since_epoch().count() != 0) {
			latencies_ms.push_back(std::chrono::duration<double, std::milli>(tp - publish_time).count());
		}
	}
	std::sort(latencies_ms.begin(), latencies_ms.end());

	std::printf("=== load/fan-out results (NOT a production benchmark; single machine, one process) ===\n");
	std::printf("watchers:                 %d\n", num_watchers);
	std::printf("total requests served:     %llu\n", static_cast<unsigned long long>(total_requests.load()));
	std::printf("  -> 200 (body sent):      %llu\n", static_cast<unsigned long long>(total_200.load()));
	std::printf("  -> 304 (conditional hit):%llu\n", static_cast<unsigned long long>(total_304.load()));
	std::printf("watchers converged to v2:  %d / %d\n", converged_count.load(), num_watchers);
	if (!latencies_ms.empty()) {
		std::printf("fan-out latency (ms): p50=%.1f p90=%.1f p99=%.1f max=%.1f\n",
		            latencies_ms[latencies_ms.size() * 50 / 100], latencies_ms[latencies_ms.size() * 90 / 100],
		            latencies_ms[std::min(latencies_ms.size() - 1, latencies_ms.size() * 99 / 100)],
		            latencies_ms.back());
	}
	std::printf("reactor threads used: %d (io) + up to 8 (offload pool, PUT-only)\n", reactors);
	std::printf("========================================================================\n");

	// The only hard assertions: correctness, not a performance SLO (no
	// unsourced latency figure is claimed anywhere in this project).
	assert(converged_count.load() == num_watchers);
	assert(total_304.load() > 0); // the conditional short-circuit actually fired

	return 0;
}
