#pragma once
#include "detent_local/ownership.hh"
#include "paged_page_validator.hh"

namespace detent::local::detail {
class SegmentRegistry;
struct SegmentInput {
	SegmentOwnership ownership;
	std::span<const char> bytes;
};
struct PagedStateCounts {
	std::uint32_t bools = 0, ints = 0;
	std::uint64_t overrides = 0, strings = 0;
	std::uint32_t maximum_overrides = 0;
};
struct PagedSemanticLimits {
	std::uint64_t strings = 128ull * 1024 * 1024;
	std::uint32_t overrides = 256;
};
struct SegmentReachability {
	std::uint64_t pages = 0, bytes = 0;
};
// The owner retains the immutable, identity-sorted registry and segment bytes.
class GraphValidator {
  public:
	GraphValidator(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> owner,
				   std::span<const SegmentInput>, paged::Root, StorageIdentity, paged::Limits = {},
				   PagedSemanticLimits = {});
	~GraphValidator();
	GraphValidator(GraphValidator &&) noexcept;
	GraphValidator(const GraphValidator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	paged::Root root() const;
	PagedStateCounts counts() const;
	SegmentReachability segment_reachability(std::size_t index) const;
	bool record_reachability(SegmentRegistry &, ValidationBudget &);
	ReadView take(distribution::RetirementDomain &);
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
