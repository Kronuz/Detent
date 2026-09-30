// Section 13 concurrency torture test (Milestone 3 acceptance criteria):
// many reader threads race one updater swapping SnapshotRef; every read must
// observe a complete, single-generation snapshot -- never partial, never
// mixed, never a crash. This exercises SnapshotRef (atomic_shared_ptr)
// directly rather than through Client, since that is the exact mechanism the
// spec's Section 3/6/7 pseudocode describes.
//
// Note: ThreadSanitizer itself was found to segfault unconditionally in this
// environment (even for a trivial std::thread hello-world with no atomics
// involved), so this is a plain concurrent stress/soak test rather than a
// TSan run. It still catches use-after-free, crashes, and cross-generation
// reads under real contention.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

#include "detent/model.hh"
#include "detent/snapshot_ref.hh"

using namespace detent;

namespace {

constexpr int kKeysPerSnapshot = 200;
constexpr int kReaderThreads = 64;
constexpr int kSwapsPerRun = 20'000;

SnapshotPtr make_generation(std::uint64_t generation) {
	auto snap = std::make_shared<Snapshot>();
	snap->version = generation;
	for (int i = 0; i < kKeysPerSnapshot; ++i) {
		IntDef def;
		// Every value in this snapshot encodes the same generation, so a
		// reader that pulls several keys from ONE acquired snapshot can
		// detect a mixed-generation read immediately.
		def.default_value = static_cast<std::int64_t>(generation);
		snap->knobs["g:k" + std::to_string(i)] = def;
	}
	return snap;
}

} // namespace

int main() {
	SnapshotRef ref;
	ref.store(make_generation(0));

	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> reads{0};

	std::vector<std::thread> readers;
	readers.reserve(kReaderThreads);
	for (int t = 0; t < kReaderThreads; ++t) {
		readers.emplace_back([&] {
			while (!stop.load(std::memory_order_relaxed)) {
				auto snap = ref.load(); // one acquire, held for the whole check
				std::this_thread::yield();
				std::int64_t first = std::get<IntDef>(snap->knobs.begin()->second).default_value;
				for (const auto& [name, def] : snap->knobs) {
					auto v = std::get<IntDef>(def).default_value;
					// Every key must agree with the first key's generation --
					// a reader must never see a partially-built or mixed map.
					assert(v == first);
				}
				reads.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	for (std::uint64_t gen = 1; gen <= kSwapsPerRun; ++gen) {
		ref.store(make_generation(gen));
		if (gen % 997 == 0) std::this_thread::yield();
	}

	stop.store(true, std::memory_order_relaxed);
	for (auto& th : readers) th.join();

	std::printf("concurrency stress OK: %llu swaps, %llu reads, no mixed-generation read, no crash\n",
	            static_cast<unsigned long long>(kSwapsPerRun), static_cast<unsigned long long>(reads.load()));
	return 0;
}
