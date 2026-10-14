#pragma once
#include "paged_overlay_assembler.hh"
#include "paged_plan.hh"
#include "paged_trie_overlay.hh"
#include "paged_warm_validator.hh"
#include <limits>
namespace detent::local::detail {
// Producers retain authorization; this shared pipeline verifies physical construction.
class SparsePlanFinalizer {
  public:
	SparsePlanFinalizer(distribution::RetirementHandle<PagedPlanState>,
						std::shared_ptr<distribution::StorageLedger>, paged::Limits,
						std::uint64_t = std::numeric_limits<std::uint64_t>::max(),
						std::optional<paged::Digest> = {});
	void start(TrieOverlay &&);
	void step(ValidationBudget &);
	bool done() const noexcept;
	bool discard(ValidationBudget &);

  private:
	enum class Phase { Reachability, Assemble, Sort, Validate, Capture, Histogram, Capacity, Seal, Done };
	distribution::RetirementHandle<PagedPlanState> plan;
	std::shared_ptr<distribution::StorageLedger> account;
	std::shared_ptr<const void> artifact_owner;
	std::optional<TrieOverlay> overlay;
	std::optional<OverlayAssembler> assembly;
	std::optional<WarmGraphValidator> validator;
	std::vector<SegmentReachability, distribution::StorageAllocator<SegmentReachability>> old_live;
	std::vector<SegmentInput, distribution::StorageAllocator<SegmentInput>> added;
	paged::Limits limits;
	std::uint64_t maximum_output_bytes, output_bytes = 0;
	std::optional<paged::Digest> expected_logical;
	std::size_t position = 0, charge = 0, sort_cursor = 0, retire_capacity_index = 0;
	Phase phase = Phase::Reachability;
	SegmentInput sort_value{};
	bool failed = false, sorting = false, retiring = false, retire_capacity_started = false;
	bool grant(std::size_t, ValidationBudget &);
};
} // namespace detent::local::detail
