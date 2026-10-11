#pragma once
#include "paged_contextual_reuse.hh"
#include "paged_cursor.hh"
namespace detent::local::detail {
// Graph validity reuses actual-base proofs; durable dependency checks remain separate.
class WarmGraphValidator {
  public:
	WarmGraphValidator(std::shared_ptr<distribution::StorageLedger>, const RecoveredGeneration &,
					   std::shared_ptr<const void> new_segments_owner,
					   std::span<const SegmentInput> new_segments, paged::Root, paged::Limits = {},
					   PagedSemanticLimits = {});
	~WarmGraphValidator();
	WarmGraphValidator(WarmGraphValidator &&) noexcept;
	WarmGraphValidator(const WarmGraphValidator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	paged::Root root() const;
	PagedStateCounts counts() const;
	SegmentReachability removed(std::size_t base_segment) const;
	SegmentReachability added(std::size_t new_segment) const;
	std::uint64_t hashed_segment_bytes() const;
	bool record_reachability(SegmentRegistry &, ValidationBudget &);
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
