// hash_stats() read while a build counts its hashes: nothing but the build hashes, so every read sees zero
// checked bytes, and a later read never sees less than an earlier one (its difference does not wrap).
#include "detent_delivery/hash_stats.hh"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace p = detent::local::paged;

int main() {
	std::atomic<bool> stop{false};
	std::thread writer([&] {
		while (!stop.load(std::memory_order_relaxed)) {
			p::count_build_hash(p::HashKind::Page, 1, 4000);
			p::count_build_hash(p::HashKind::Pack, 1, 350000);
		}
	});
	std::uint64_t reads = 0, torn = 0;
	auto previous = detent::delivery::hash_stats();
	const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < end && torn == 0) {
		const auto current = detent::delivery::hash_stats();
		const auto step = current - previous;
		++reads;
		if (current.checked_bytes() != 0 || step.checked_bytes() != 0 || current.built_bytes() < previous.built_bytes())
			++torn;
		previous = current;
	}
	stop = true;
	writer.join();
	std::cout << "hash-stats reads=" << reads << " torn=" << torn
			  << " built_bytes=" << detent::delivery::hash_stats().built_bytes() << '\n';
	return torn ? 1 : 0;
}
