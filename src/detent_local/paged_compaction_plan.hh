#pragma once
#include "paged_plan.hh"
namespace detent::local::detail {
struct CompactionLimits {
	std::size_t maximum_packs = 8, maximum_recipes = 32768;
	std::uint64_t maximum_output_bytes = 16ull * 1024 * 1024;
};
// Physical replacement retains actual base authority and preserves the complete cursor.
class PagedCompactionBuilder {
  public:
	PagedCompactionBuilder(const RecoveredGeneration &, std::span<const StorageIdentity>, GenerationId,
						   distribution::RetirementDomain &, CompactionLimits = {}, paged::Limits = {});
	PagedCompactionBuilder(const RecoveredGeneration &, std::span<const StorageIdentity>, GenerationId,
						   distribution::RetirementFactory, std::shared_ptr<distribution::StorageLedger>,
						   CompactionLimits = {}, paged::Limits = {});
	~PagedCompactionBuilder();
	PagedCompactionBuilder(PagedCompactionBuilder &&) noexcept;
	PagedCompactionBuilder(const PagedCompactionBuilder &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedPlan take();
	std::shared_ptr<const StateEquivalenceProof> evidence() const;
	bool discard(ValidationBudget &);
	std::uint64_t visited_pages() const;
	std::size_t relocated_pages() const;

  private:
	struct Work;
	distribution::RetirementHandle<Work> work_;
	distribution::RetirementHandle<PagedPlanState> plan_;
};
} // namespace detent::local::detail
