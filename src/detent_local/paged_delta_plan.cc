#include "paged_delta_plan.hh"
#include "../detent_distribution/source_input.hh"
#include "paged_delta_leaves.hh"
#include "sparse_plan_finalizer.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
struct PagedDeltaPlanBuilder::Work final : d::RetirementNode {
	d::RetirementHandle<PagedPlanState> plan;
	std::shared_ptr<const d::ValidatedDelta> source;
	std::optional<DeltaLeaves> leaves;
	std::optional<TrieOverlay> overlay;
	SparsePlanFinalizer finalizer;
	bool applying = false, finalizing = false, failed = false;
	Work(d::RetirementHandle<PagedPlanState> state, const RecoveredGeneration &base,
		 std::shared_ptr<const d::ValidatedDelta> delta, p::Limits limits)
		: plan(std::move(state)), source(std::move(delta)),
		  finalizer(plan, source->storage_account(), limits) {
		const auto account = source->storage_account();
		using Base = d::RetirementHandle<LeasedPagedRoot>;
		auto owner = std::allocate_shared<Base>(d::StorageAllocator<Base>(account), plan->base_);
		leaves.emplace(base, source, limits);
		overlay.emplace(account, std::move(owner), plan->registry_->inputs(), plan->base_->root(), limits);
	}
	void step(ValidationBudget &budget) {
		if (failed)
			throw std::logic_error("failed paged delta construction");
		try {
			while (!finalizing) {
				if (applying) {
					overlay->step(budget);
					if (!overlay->operation_done())
						return;
					leaves->consume();
					applying = false;
				}
				leaves->step(budget);
				if (leaves->done()) {
					overlay->seal();
					finalizer.start(std::move(*overlay));
					overlay.reset();
					finalizing = true;
					break;
				}
				if (!leaves->ready())
					return;
				const auto leaf = leaves->leaf();
				overlay->change(leaf.key, leaf.body);
				applying = true;
			}
			finalizer.step(budget);
			if (finalizer.done())
				plan->sealed_ = true;
		} catch (...) {
			failed = true;
			throw;
		}
	}
	bool retire_step() noexcept override {
		ValidationBudget budget{8192, 16};
		if (!finalizer.discard(budget))
			return false;
		if (leaves && !leaves->discard(budget))
			return false;
		leaves.reset();
		if (overlay && !overlay->discard(budget))
			return false;
		overlay.reset();
		plan.reset();
		source.reset();
		return true;
	}
};
PagedDeltaPlanBuilder::PagedDeltaPlanBuilder(const RecoveredGeneration &base, d::ValidatedDelta &&input,
											 GenerationId generation, d::RetirementDomain &domain,
											 p::Limits limits) {
	if (!base.paged_ || input.storage_account() != domain.storage_account())
		throw std::invalid_argument("paged delta plan requires actual base and common admission");
	using Input = d::detail::RetiredInput<d::ValidatedDelta>;
	auto source = domain.make<Input>(std::move(input));
	using Handle = d::RetirementHandle<Input>;
	auto pin = std::allocate_shared<Handle>(d::StorageAllocator<Handle>(domain.storage_account()), source);
	std::shared_ptr<const d::ValidatedDelta> delta(pin, &source->value);
	plan_ = domain.make<PagedPlanState>(domain.storage_account(), base.paged_, generation, *delta,
										PagedPlanState::Forward{});
	work_ = domain.make<Work>(plan_, base, std::move(delta), limits);
}
PagedDeltaPlanBuilder::~PagedDeltaPlanBuilder() = default;
PagedDeltaPlanBuilder::PagedDeltaPlanBuilder(PagedDeltaPlanBuilder &&) noexcept = default;
void PagedDeltaPlanBuilder::step(ValidationBudget &budget) {
	if (!work_ || !plan_)
		throw std::logic_error("inactive paged delta plan builder");
	work_->step(budget);
}
bool PagedDeltaPlanBuilder::done() const noexcept {
	return work_ && plan_ && !work_->failed && plan_->sealed_;
}
PagedPlan PagedDeltaPlanBuilder::take() {
	if (!done())
		throw std::logic_error("paged delta plan incomplete");
	work_.reset();
	return PagedPlan(std::move(plan_));
}
bool PagedDeltaPlanBuilder::discard(ValidationBudget &budget) {
	if (!work_ && !plan_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	work_.reset();
	plan_.reset();
	return true;
}
} // namespace detent::local::detail
