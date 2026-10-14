#pragma once
#include "owned_segment_inputs.hh"
#include "paged_snapshot_builder.hh"
#include "validated_paged_state.hh"
#include <sha256.h>
namespace detent::local::detail {
// Complete replacement images contain only newly encoded immutable packs.
class CandidatePagedValidator {
  public:
	CandidatePagedValidator(PagedImage &&, distribution::RetirementFactory,
							std::shared_ptr<distribution::StorageLedger>, StorageIdentity, ViewLimits);
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedStateCounts counts() const;
	SegmentReachability segment_reachability(std::size_t) const;
	SegmentReachability segment_reachability(const StorageIdentity &) const;
	std::span<const std::uint32_t, 257> override_histogram() const;
	const distribution::RetirementHandle<PagedImageOwner> &image() const noexcept { return image_; }
	ValidatedPagedState take();

  private:
	distribution::RetirementFactory factory_;
	std::shared_ptr<distribution::StorageLedger> heap_;
	ViewLimits limits_;
	distribution::RetirementHandle<PagedImageOwner> image_;
	OwnedSegmentInputs inputs_;
	std::optional<GraphValidator> graph_;
	std::size_t next_ = 0;
	StorageIdentity storage_;
	class SHA256 hash_;
	std::size_t hashed_ = 0;
	bool manifest_checked_ = false;
};
} // namespace detent::local::detail
