// detent_read_bench -- the read path's steady-state throughput: N threads
// hammering Client::get()/eval() against one warm, never-updated snapshot
// (no background source configured). This is the number that matters most
// for the whole design: every consuming service calls this on its hot path,
// so it has to be cheap regardless of thread count.
//
//   cmake --build build && ./build/detent_read_bench [threads] [ops-per-thread]

#include "detent/client.hh"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
	std::size_t threads = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 4;
	std::uint64_t ops_per_thread = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 2'000'000;

	auto defaults_path = std::filesystem::temp_directory_path() / "detent_read_bench_defaults.json";
	{
		std::ofstream out(defaults_path, std::ios::binary);
		out << R"({
			"schema_version": 1,
			"knobs": {
				"checkout:killswitch_new_path": { "type": "bool", "default": false },
				"search:max_results": { "type": "int", "default": 25 },
				"payments:new_processor": { "type": "bool", "default": false, "rollout_pct": 10 }
			}
		})";
	}

	detent::InitOptions opts;
	opts.defaults_path = defaults_path; // no source_path/source_poll: no background thread at all
	detent::Client client(std::move(opts));

	std::atomic<bool> start{false};
	std::atomic<std::uint64_t> total_ok{0};
	std::vector<std::thread> pool;
	for (std::size_t t = 0; t < threads; ++t) {
		pool.emplace_back([&, t] {
			while (!start.load(std::memory_order_acquire)) {}
			std::uint64_t ok = 0;
			for (std::uint64_t i = 0; i < ops_per_thread; ++i) {
				// Alternate a plain int get() and a rollout eval() -- the two
				// shapes real callers actually use -- keyed so each thread's
				// "user id" differs, exercising different hash buckets.
				if (i & 1) {
					auto r = client.get("search:max_results");
					ok += r.has_value();
				} else {
					auto id = std::to_string(t * 1'000'003 + i);
					auto r = client.eval("payments:new_processor", id);
					ok += r.has_value();
				}
			}
			total_ok.fetch_add(ok, std::memory_order_relaxed);
		});
	}

	auto t0 = Clock::now();
	start.store(true, std::memory_order_release);
	for (auto& th : pool) th.join();
	auto elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

	auto total_ops = threads * ops_per_thread;
	std::printf(
		"detent_read_bench: %zu threads x %llu ops = %llu total, %.0f ops/sec (%.1f ns/op), %llu succeeded\n",
		threads, (unsigned long long)ops_per_thread, (unsigned long long)total_ops,
		total_ops / elapsed, elapsed * 1e9 / total_ops, (unsigned long long)total_ok.load());

	std::filesystem::remove(defaults_path);
	return total_ok.load() == total_ops ? 0 : 1;
}
