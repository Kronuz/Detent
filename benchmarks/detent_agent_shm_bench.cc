// detent_agent_shm_bench -- the shm reader's steady-state throughput: N
// threads hammering ShmReader::get()/eval() against one warm, never-
// updated snapshot published by a real ShmWriter. This is the number that
// justifies the mmap/seqlock redesign over the old Unix-socket protocol
// (see README's IPC-design-tradeoffs section): a client process never
// round-trips to the daemon at all, so its cost floor is a handful of
// atomic loads plus a binary search, not a syscall.
//
//   cmake --build build && ./build/benchmarks/detent_agent_shm_bench [threads] [ops-per-thread]

#include "shm_reader.hh"
#include "shm_writer.hh"

#include "detent/model.hh"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
	std::size_t threads = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 4;
	std::uint64_t ops_per_thread = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 2'000'000;

	auto shm_path = std::filesystem::temp_directory_path() / "detent_agent_shm_bench.shm";
	std::filesystem::remove(shm_path);

	detent::Snapshot snapshot;
	snapshot.version = 1;
	{
		detent::BoolDef killswitch;
		killswitch.default_value = false;
		snapshot.knobs.emplace("checkout:killswitch_new_path", killswitch);

		detent::IntDef max_results;
		max_results.default_value = 25;
		snapshot.knobs.emplace("search:max_results", max_results);

		detent::BoolDef rollout;
		rollout.default_value = false;
		rollout.rollout_pct = 10;
		snapshot.knobs.emplace("payments:new_processor", rollout);
	}

	{
		detent_agent::ShmWriter writer(shm_path);
		if (!writer.publish(snapshot)) {
			std::fprintf(stderr, "detent_agent_shm_bench: initial publish failed\n");
			return 1;
		}
	} // writer's mmap/fd are only needed to seed the file; the reader mmaps its own

	auto reader_opt = detent_agent::ShmReader::open(shm_path);
	if (!reader_opt) {
		std::fprintf(stderr, "detent_agent_shm_bench: reader open() failed\n");
		return 1;
	}
	const auto& reader = *reader_opt;

	std::atomic<bool> start{false};
	std::atomic<std::uint64_t> total_ok{0};
	std::vector<std::thread> pool;
	for (std::size_t t = 0; t < threads; ++t) {
		pool.emplace_back([&, t] {
			while (!start.load(std::memory_order_acquire)) {}
			std::uint64_t ok = 0;
			for (std::uint64_t i = 0; i < ops_per_thread; ++i) {
				// Same two shapes detent_read_bench exercises, so the two
				// numbers are directly comparable apples-to-apples.
				if (i & 1) {
					auto r = reader.get("search:max_results", std::nullopt);
					ok += r.has_value();
				} else {
					auto id = std::to_string(t * 1'000'003 + i);
					auto r = reader.eval("payments:new_processor", std::string_view(id), std::nullopt);
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
		"detent_agent_shm_bench: %zu threads x %llu ops = %llu total, %.0f ops/sec (%.1f ns/op), %llu succeeded\n",
		threads, (unsigned long long)ops_per_thread, (unsigned long long)total_ops,
		total_ops / elapsed, elapsed * 1e9 / total_ops, (unsigned long long)total_ok.load());

	std::filesystem::remove(shm_path);
	return total_ok.load() == total_ops ? 0 : 1;
}
