// detent_hash_bench -- throughput of stable_bucket(), the SHA-256-backed
// rollout hash every eval() call on a percentage-rollout knob makes. Single-
// threaded: this function has no shared state, so N threads just multiply
// the number below, they don't change it.
//
//   cmake --build build && ./build/detent_hash_bench [iterations]

#include "detent/hashing.hh"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
	std::uint64_t iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5'000'000;

	// Realistic inputs: a namespaced knob name, and a hash_val shaped like a
	// user/entity ID, not a fixed string -- stable_bucket's cost shouldn't
	// (and per its SHA-256 basis, doesn't) depend on the specific bytes.
	std::uint64_t sink = 0;
	auto t0 = Clock::now();
	for (std::uint64_t i = 0; i < iterations; ++i) {
		auto id = std::to_string(i);
		sink += detent::stable_bucket("payments:new_processor", id);
	}
	auto elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

	std::printf("detent_hash_bench: %llu calls, %.0f calls/sec (%.1f ns/call), sink=%llu (prevents dead-code elimination)\n",
	            (unsigned long long)iterations, iterations / elapsed, elapsed * 1e9 / iterations,
	            (unsigned long long)sink);
	return 0;
}
