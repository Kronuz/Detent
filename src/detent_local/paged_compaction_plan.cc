#include "paged_compaction_plan.hh"
#include "sparse_plan_finalizer.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
struct PagedCompactionBuilder::Work final : d::RetirementNode {
	d::RetirementHandle<PagedPlanState> plan;
	RecoveredGeneration base;
	std::optional<TrieOverlay> overlay;
	SparsePlanFinalizer finalizer;
	std::uint64_t visited = 0;
	std::size_t relocated = 0;
	bool finalizing = false, failed = false;
	Work(d::RetirementHandle<PagedPlanState> state, const RecoveredGeneration &generation,
		 std::shared_ptr<d::StorageLedger> heap, std::span<const StorageIdentity> selected,
		 CompactionLimits compaction, p::Limits limits)
		: plan(std::move(state)), base(generation),
		  finalizer(plan, heap, limits, compaction.maximum_output_bytes, plan->base_->root().logical) {
		using Base = d::RetirementHandle<LeasedPagedRoot>;
		auto owner = std::allocate_shared<Base>(d::StorageAllocator<Base>(heap), plan->base_);
		overlay.emplace(heap, std::move(owner), plan->registry_->inputs(), plan->base_->root(), limits);
		overlay->begin_relocation(selected, compaction.maximum_recipes);
	}
	void step(ValidationBudget &budget) {
		if (failed)
			throw std::logic_error("failed paged compaction construction");
		try {
			if (!finalizing) {
				overlay->step(budget);
				if (!overlay->operation_done())
					return;
				visited = overlay->visited_pages();
				relocated = overlay->relocated_pages();
				overlay->seal();
				finalizer.start(std::move(*overlay));
				overlay.reset();
				finalizing = true;
			}
			finalizer.step(budget);
			if (!finalizer.done())
				return;
			if (plan->root_.cursor != base.descriptor().cursor ||
				plan->root_.profile != base.descriptor().profile ||
				plan->root_.logical != plan->base_->root().logical)
				throw std::invalid_argument("compaction semantic equality mismatch");
			plan->maintenance_proof_.emplace(
				StateEquivalenceProof(base.descriptor(), plan->descriptor_, base.directory_identity(), {}));
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
		if (overlay && !overlay->discard(budget))
			return false;
		overlay.reset();
		plan.reset();
		return true;
	}
};
PagedCompactionBuilder::PagedCompactionBuilder(const RecoveredGeneration &base,
											   std::span<const StorageIdentity> selected,
											   GenerationId generation, d::RetirementDomain &domain,
											   CompactionLimits compaction, p::Limits limits) {
	if (!base.paged_ || selected.empty() || selected.size() > compaction.maximum_packs ||
		compaction.maximum_packs > 8 || !compaction.maximum_recipes || !compaction.maximum_output_bytes)
		throw std::invalid_argument("compaction selection or admission bound mismatch");
	plan_ = domain.make<PagedPlanState>(domain.storage_account(), base.paged_, generation,
										PagedPlanState::PhysicalMaintenance{});
	work_ = domain.make<Work>(plan_, base, domain.storage_account(), selected, compaction, limits);
}
PagedCompactionBuilder::~PagedCompactionBuilder() = default;
PagedCompactionBuilder::PagedCompactionBuilder(const RecoveredGeneration &base,
											   std::span<const StorageIdentity> selected,
											   GenerationId generation, d::RetirementFactory factory,
											   std::shared_ptr<d::StorageLedger> heap,
											   CompactionLimits compaction, p::Limits limits) {
	if (!base.paged_ || !factory.uses_account(heap) || selected.empty() ||
		selected.size() > compaction.maximum_packs || compaction.maximum_packs > 8 ||
		!compaction.maximum_recipes || !compaction.maximum_output_bytes)
		throw std::invalid_argument("compaction captured resources or selection mismatch");
	plan_ =
		factory.make<PagedPlanState>(heap, base.paged_, generation, PagedPlanState::PhysicalMaintenance{});
	work_ = factory.make<Work>(plan_, base, std::move(heap), selected, compaction, limits);
}
std::shared_ptr<const StateEquivalenceProof> PagedCompactionBuilder::evidence() const {
	if (!done() || !plan_->maintenance_proof_)
		throw std::logic_error("compaction evidence unavailable");
	using Handle = d::RetirementHandle<PagedPlanState>;
	auto owner = std::allocate_shared<Handle>(d::StorageAllocator<Handle>(plan_->bytes()->account()), plan_);
	return {std::move(owner), &*plan_->maintenance_proof_};
}
PagedCompactionBuilder::PagedCompactionBuilder(PagedCompactionBuilder &&) noexcept = default;
void PagedCompactionBuilder::step(ValidationBudget &budget) {
	if (!work_ || !plan_)
		throw std::logic_error("inactive paged compaction plan builder");
	work_->step(budget);
}
bool PagedCompactionBuilder::done() const noexcept {
	return work_ && plan_ && !work_->failed && plan_->sealed_;
}
PagedPlan PagedCompactionBuilder::take() {
	if (!done())
		throw std::logic_error("paged compaction plan incomplete");
	work_.reset();
	return PagedPlan(std::move(plan_));
}
std::uint64_t PagedCompactionBuilder::visited_pages() const {
	if (!work_)
		throw std::logic_error("compaction metrics unavailable");
	return work_->finalizing ? work_->visited : work_->overlay->visited_pages();
}
std::size_t PagedCompactionBuilder::relocated_pages() const {
	if (!work_)
		throw std::logic_error("compaction metrics unavailable");
	return work_->finalizing ? work_->relocated : work_->overlay->relocated_pages();
}
bool PagedCompactionBuilder::discard(ValidationBudget &budget) {
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
