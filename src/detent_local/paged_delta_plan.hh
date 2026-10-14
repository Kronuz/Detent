#pragma once
#include "paged_plan.hh"
namespace detent::local::detail {
// Input and construction scratch retire off the caller after transfer or cancellation.
class PagedDeltaPlanBuilder {
  public:
	PagedDeltaPlanBuilder(const RecoveredGeneration &, distribution::ValidatedDelta &&, GenerationId,
						  distribution::RetirementDomain &, paged::Limits = {});
	PagedDeltaPlanBuilder(const RecoveredGeneration &, std::shared_ptr<const distribution::ValidatedDelta>,
						  GenerationId, distribution::RetirementFactory,
						  std::shared_ptr<distribution::StorageLedger>, paged::Limits = {});
	~PagedDeltaPlanBuilder();
	PagedDeltaPlanBuilder(PagedDeltaPlanBuilder &&) noexcept;
	PagedDeltaPlanBuilder(const PagedDeltaPlanBuilder &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedPlan take();
	bool discard(ValidationBudget &);

  private:
	struct Work;
	distribution::RetirementHandle<PagedPlanState> plan_;
	distribution::RetirementHandle<Work> work_;
};
} // namespace detent::local::detail
