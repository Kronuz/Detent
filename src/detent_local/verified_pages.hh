#pragma once
#include "detent_distribution/memory_resource.hh"
#include "detent_local/paged_layout.hh"
#include <atomic>
#include <vector>
#include <stdexcept>

namespace detent::local::detail {
// One nibble per 80-byte granule records its exact aligned verified start.
// Disjoint pages span at least 79 bytes; release pairs with acquire on test.
class VerifiedPages {
  public:
	static constexpr std::size_t granule_bytes = 80;
	VerifiedPages(std::size_t segment_bytes, const std::shared_ptr<distribution::StorageLedger> &account)
		: words_((segment_bytes / granule_bytes + 16) / 16,
				 distribution::StorageAllocator<std::atomic<std::uint64_t>>(account)) {}
	bool test(std::size_t offset) const noexcept {
		const auto index = offset / granule_bytes / 16;
		return offset % 8 == 0 && index < words_.size() &&
			((words_[index].load(std::memory_order_acquire) >> shift(offset)) & 15) == code(offset);
	}
	void set(std::size_t offset) const {
		const auto index = offset / granule_bytes / 16;
		if (offset % 8 || index >= words_.size())
			throw std::invalid_argument("invalid verified page start");
		auto &word = words_[index];
		auto old = word.load(std::memory_order_acquire);
		for (;;) {
			const auto prior = (old >> shift(offset)) & 15;
			if (prior) {
				if (prior != code(offset))
					throw std::invalid_argument("overlapping verified page starts");
				return;
			}
			if (word.compare_exchange_weak(old, old | (code(offset) << shift(offset)),
									 std::memory_order_release, std::memory_order_acquire))
				return;
		}
	}
	// Withdraws every verified page, so the next read of each re-verifies it. A first touch racing the
	// clear can leave its own page set; the scrub's next pass over the pack reports the damage again.
	void clear() const noexcept {
		for (auto &word : words_)
			word.store(0, std::memory_order_release);
	}

  private:
	static unsigned shift(std::size_t offset) noexcept { return (offset / granule_bytes % 16) * 4; }
	static std::uint64_t code(std::size_t offset) noexcept { return 1 + offset % granule_bytes / 8; }

	mutable std::vector<std::atomic<std::uint64_t>,
						distribution::StorageAllocator<std::atomic<std::uint64_t>>>
		words_;
};
} // namespace detent::local::detail
