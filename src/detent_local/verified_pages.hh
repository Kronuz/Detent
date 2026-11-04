#pragma once
#include "detent_distribution/memory_resource.hh"
#include "detent_local/paged_layout.hh"
#include <atomic>
#include <vector>

namespace detent::local::detail {
// One bit per 80-byte granule of a mapped segment, set once the page there has passed first touch.
// Validated pages are disjoint, start 8-aligned and span at least one header, so distinct pages own
// distinct granules. Release on set pairs with acquire on test.
class VerifiedPages {
  public:
	static constexpr std::size_t granule_bytes = paged::page_header_bytes;
	VerifiedPages(std::size_t segment_bytes, const std::shared_ptr<distribution::StorageLedger> &account)
		: words_((segment_bytes / granule_bytes + 64) / 64,
				 distribution::StorageAllocator<std::atomic<std::uint64_t>>(account)) {}
	bool test(std::size_t offset) const noexcept {
		return words_[offset / granule_bytes / 64].load(std::memory_order_acquire) & mask(offset);
	}
	void set(std::size_t offset) const noexcept {
		words_[offset / granule_bytes / 64].fetch_or(mask(offset), std::memory_order_release);
	}

  private:
	static std::uint64_t mask(std::size_t offset) noexcept {
		return std::uint64_t(1) << (offset / granule_bytes % 64);
	}
	mutable std::vector<std::atomic<std::uint64_t>,
						distribution::StorageAllocator<std::atomic<std::uint64_t>>>
		words_;
};
} // namespace detent::local::detail
