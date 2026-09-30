// Dedicated reader-vs-writer concurrency stress test for the shm channel:
// several reader threads hammering ShmReader::get() while one writer thread
// republishes strictly-increasing snapshots via ShmWriter::publish()
// directly (no detent::Client/Agent in the loop -- this isolates the
// seqlock protocol itself, the thing least like the rest of the codebase
// and most worth a dedicated TSan-able test). Each publish sets
// search:max_results' default to the publish's own version number, so a
// reader's successive observations must be monotonically non-decreasing --
// since there is exactly one writer publishing strictly increasing values
// and the shm always holds only the latest one, any decrease a reader
// observes is only possible if it read a torn/inconsistent mix of old and
// new bytes, which is exactly what the seqlock is supposed to prevent.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>

#include "shm_reader.hh"
#include "shm_writer.hh"

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path() {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("detent_shm_stress_" + std::to_string(rng()) + ".shm");
}

detent::Snapshot make_snapshot(std::uint64_t version) {
	detent::Snapshot snap;
	snap.version = version;
	snap.source_id = "stress";
	detent::IntDef def;
	def.default_value = static_cast<std::int64_t>(version);
	snap.knobs["search:max_results"] = def;
	return snap;
}

} // namespace

int main() {
	auto path = temp_path();
	detent_agent::ShmWriter writer(path);
	writer.publish(make_snapshot(0));

	constexpr int kNumReaders = 8;
	constexpr std::uint64_t kNumPublishes = 20'000;

	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> total_reads{0};
	std::vector<std::thread> readers;
	std::vector<std::atomic<bool>> reader_failed(kNumReaders);
	for (auto& f : reader_failed) f.store(false);

	for (int r = 0; r < kNumReaders; ++r) {
		readers.emplace_back([&, r] {
			auto reader = detent_agent::ShmReader::open(path);
			if (!reader) {
				reader_failed[static_cast<std::size_t>(r)].store(true);
				return;
			}
			std::int64_t last_seen = 0;
			std::uint64_t reads = 0;
			while (!stop.load(std::memory_order_relaxed)) {
				auto result = reader->get("search:max_results", std::nullopt);
				if (!result.has_value()) {
					reader_failed[static_cast<std::size_t>(r)].store(true);
					return;
				}
				if (result.value() < last_seen) {
					// A real torn read: never allowed, regardless of timing.
					reader_failed[static_cast<std::size_t>(r)].store(true);
					return;
				}
				last_seen = result.value();
				++reads;
			}
			total_reads.fetch_add(reads, std::memory_order_relaxed);
		});
	}

	for (std::uint64_t v = 1; v <= kNumPublishes; ++v) {
		writer.publish(make_snapshot(v));
	}
	stop.store(true, std::memory_order_relaxed);
	for (auto& t : readers) t.join();

	for (int r = 0; r < kNumReaders; ++r) {
		assert(!reader_failed[static_cast<std::size_t>(r)].load());
	}

	std::filesystem::remove(path);
	std::printf("shm concurrency stress: %d readers x %llu total reads racing %llu publishes, no torn read OK\n",
	            kNumReaders, (unsigned long long)total_reads.load(), (unsigned long long)kNumPublishes);
	return 0;
}
