#pragma once
#include "paged_snapshot_builder.hh"
#include "paged_trie_overlay.hh"
namespace detent::local::detail {
// Worker-only assembly borrows a sealed overlay and checked base statistics.
class OverlayAssembler {
  public:
	OverlayAssembler(std::shared_ptr<distribution::StorageLedger>, const TrieOverlay &,
					 std::span<const SegmentInput>, std::span<const SegmentReachability>, paged::Root,
					 StorageIdentity, GenerationId, distribution::Cursor target, paged::Limits = {});
	~OverlayAssembler();
	OverlayAssembler(OverlayAssembler &&) noexcept;
	OverlayAssembler(const OverlayAssembler &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedImage take();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
