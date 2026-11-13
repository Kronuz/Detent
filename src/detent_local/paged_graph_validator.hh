#pragma once
#include "detent_local/ownership.hh"
#include "paged_page_validator.hh"

namespace detent::local::detail {
class SegmentRegistry;
class VerifiedPages;
struct SegmentInput {
	SegmentOwnership ownership;
	std::span<const char> bytes;
	// Non-null while no hash has compared this segment's pages: reads then verify a page on first touch.
	const VerifiedPages *pages = nullptr;
};
struct PagedStateCounts {
	std::uint32_t records = 0;
	std::uint64_t overrides = 0, strings = 0;
	std::uint32_t maximum_overrides = 0;
};
// The string and override bounds a validation enforces: the store's or the format's, never a default.
struct PagedSemanticLimits {
	constexpr PagedSemanticLimits(std::uint64_t strings, std::uint32_t overrides) : strings(strings), overrides(overrides) {}
	std::uint64_t strings;
	std::uint32_t overrides;
};
struct SegmentReachability {
	std::uint64_t pages = 0, bytes = 0;
};
// The owner retains the immutable, identity-sorted registry and segment bytes. A registry whose
// discovery certified its digests for this root (SegmentRegistry::certify_digests) may be passed as
// `compared`: segment checksums and page digests are then not recomputed. `own_build` says the graph is the
// image this process just built, whose digests the build computed: they are not compared either.
class GraphValidator {
  public:
	GraphValidator(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> owner,
				   std::span<const SegmentInput>, paged::Root, paged::Limits, PagedSemanticLimits,
				   const SegmentRegistry *compared = nullptr, bool own_build = false);
	~GraphValidator();
	GraphValidator(GraphValidator &&) noexcept;
	GraphValidator(const GraphValidator &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	paged::Root root() const;
	PagedStateCounts counts() const;
	std::uint64_t hashed_segment_bytes() const noexcept;
	std::pair<std::shared_ptr<const void>, std::span<const SegmentInput>> validated_inputs() const;
	SegmentReachability segment_reachability(std::size_t index) const;
	bool record_reachability(SegmentRegistry &, ValidationBudget &);
	std::span<const std::uint32_t, 257> override_histogram() const;
	ReadView take(distribution::RetirementDomain &);
	ReadView take(distribution::RetirementFactory, std::shared_ptr<distribution::StorageLedger>);
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
