#pragma once
#include "paged_plan.hh"
#include "paged_transfer.hh"
#include "sparse_plan_finalizer.hh"

namespace detent::local::detail {
// A delta transfer over the selected base, published as a plan over that base (dense distribution
// D6, D9): the new packs are the received ones, named by next_generation over the base; the base
// keeps every page the new tree still reaches. The removed base pages come from a walk of the base
// from its root that stops at the pages the transfer reuses, and the warm validator must find the
// same removal. No packer runs.
class PagedTransferPlanBuilder {
  public:
	// `received` is DD1's receive over `base` with transfer_segment_ids(name of the delta's generation).
	PagedTransferPlanBuilder(const RecoveredGeneration &base, ReceivedTransfer &&received,
							 const distribution::NamespaceCursor &target, distribution::RetirementFactory,
							 std::shared_ptr<distribution::StorageLedger>, paged::Limits);
	~PagedTransferPlanBuilder();
	PagedTransferPlanBuilder(PagedTransferPlanBuilder &&) noexcept;
	PagedTransferPlanBuilder(const PagedTransferPlanBuilder &) = delete;
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
