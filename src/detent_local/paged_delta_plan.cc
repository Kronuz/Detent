#include "paged_delta_plan.hh"
#include "../detent_distribution/source_input.hh"
#include "paged_delta_leaves.hh"
#include "paged_navigation.hh"
#include "paged_overlay_assembler.hh"
#include "paged_warm_validator.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
struct PagedDeltaPlanBuilder::Work final : d::RetirementNode {
	enum class Phase {
		Leaves,
		Apply,
		Reachability,
		Assemble,
		Sort,
		Validate,
		Capture,
		Histogram,
		Capacity,
		Seal,
		Done
	};
	d::RetirementHandle<PagedPlanState> plan;
	RecoveredGeneration base;
	std::shared_ptr<const d::ValidatedDelta> source;
	std::shared_ptr<const void> base_owner, artifact_owner;
	std::optional<DeltaLeaves> leaves;
	std::optional<TrieOverlay> overlay;
	std::optional<OverlayAssembler> assembly;
	std::optional<WarmGraphValidator> validator;
	std::vector<SegmentReachability, d::StorageAllocator<SegmentReachability>> old_live;
	std::vector<SegmentInput, d::StorageAllocator<SegmentInput>> added;
	p::Limits limits;
	std::size_t position = 0, charge = 0;
	Phase phase = Phase::Leaves;
	SegmentInput sort_value{};
	std::size_t sort_cursor = 0, retire_capacity_index = 0;
	bool failed = false, sorting = false, retiring = false, retire_capacity_started = false;
	Work(d::RetirementHandle<PagedPlanState> state, const RecoveredGeneration &generation,
		 std::shared_ptr<const d::ValidatedDelta> delta, p::Limits bounds)
		: plan(std::move(state)), base(generation), source(std::move(delta)),
		  old_live(d::StorageAllocator<SegmentReachability>(source->storage_account())),
		  added(d::StorageAllocator<SegmentInput>(source->storage_account())), limits(bounds) {
		const auto account = source->storage_account();
		using Base = d::RetirementHandle<LeasedPagedRoot>;
		base_owner = std::allocate_shared<Base>(d::StorageAllocator<Base>(account), plan->base_);
		leaves.emplace(base, source, limits);
		overlay.emplace(account, base_owner, plan->registry_->inputs(), plan->base_->root(), limits);
		old_live.reserve(plan->registry_->inputs().size());
	}
	bool grant(std::size_t bytes, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = bytes;
		auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	void step(ValidationBudget &budget) {
		if (failed)
			throw std::logic_error("failed paged delta construction");
		try {
			while (phase != Phase::Done) {
				switch (phase) {
				case Phase::Leaves:
					leaves->step(budget);
					if (leaves->done()) {
						overlay->seal();
						phase = Phase::Reachability;
						break;
					}
					if (!leaves->ready())
						return;
					{
						const auto leaf = leaves->leaf();
						overlay->change(leaf.key, leaf.body);
						phase = Phase::Apply;
					}
					break;
				case Phase::Apply:
					overlay->step(budget);
					if (!overlay->operation_done())
						return;
					leaves->consume();
					phase = Phase::Leaves;
					break;
				case Phase::Reachability:
					if (position < plan->registry_->inputs().size()) {
						if (!grant(sizeof(SegmentReachability), budget))
							return;
						old_live.push_back(plan->registry_->reachability(position++));
						break;
					}
					assembly.emplace(source->storage_account(), *overlay, plan->registry_->inputs(), old_live,
									 plan->base_->root(), plan->base_->storage_identity(),
									 plan->descriptor_.generation, plan->descriptor_.cursor, limits);
					position = 0;
					phase = Phase::Assemble;
					break;
				case Phase::Assemble:
					assembly->step(budget);
					if (!assembly->done())
						return;
					plan->image_.emplace(assembly->take());
					added.reserve(plan->image_->segments().size());
					phase = Phase::Validate;
					break;
				case Phase::Validate:
					if (position < plan->image_->segments().size()) {
						if (!grant(sizeof(SegmentInput), budget))
							return;
						const auto &segment = plan->image_->segments()[position++];
						added.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
						break;
					}
					if (!validator) {
						position = 1;
						phase = Phase::Sort;
						break;
					}
					validator->step(budget);
					if (!validator->done())
						return;
					plan->base_live_.reserve(old_live.size());
					plan->new_live_.reserve(added.size());
					position = 0;
					phase = Phase::Capture;
					break;
				case Phase::Sort:
					if (position < added.size()) {
						if (!grant(sizeof(SegmentInput), budget))
							return;
						if (!sorting) {
							sort_value = added[position];
							sort_cursor = position;
							sorting = true;
						}
						if (sort_cursor && std::memcmp(added[sort_cursor - 1].ownership.segment.data(),
													   sort_value.ownership.segment.data(), 16) > 0) {
							added[sort_cursor] = added[sort_cursor - 1];
							--sort_cursor;
						} else {
							added[sort_cursor] = sort_value;
							++position;
							sorting = false;
						}
						break;
					}
					{
						using State = d::RetirementHandle<PagedPlanState>;
						artifact_owner = std::allocate_shared<State>(
							d::StorageAllocator<State>(source->storage_account()), plan);
						validator.emplace(source->storage_account(), base, artifact_owner, added,
										  plan->image_->root(), limits);
					}
					phase = Phase::Validate;
					break;
				case Phase::Capture:
					if (position < old_live.size()) {
						if (!grant(sizeof(SegmentReachability), budget))
							return;
						const auto removed = validator->removed(position);
						const auto expected = overlay->removed()[position];
						if (removed.pages != expected.pages || removed.bytes != expected.bytes)
							throw std::logic_error("sparse construction removal proof mismatch");
						plan->base_live_.push_back({old_live[position].pages - removed.pages,
													old_live[position].bytes - removed.bytes});
						++position;
						break;
					}
					if (position < old_live.size() + added.size()) {
						if (!grant(sizeof(SegmentReachability), budget))
							return;
						const auto i = position++ - old_live.size();
						const auto sorted =
							find_segment(added, plan->image_->segments()[i].ownership.segment);
						plan->new_live_.push_back(validator->added(sorted));
						break;
					}
					position = 0;
					phase = Phase::Histogram;
					break;
				case Phase::Histogram:
					if (position < plan->histogram_.size()) {
						if (!grant(4, budget))
							return;
						plan->histogram_[position] = validator->override_histogram()[position];
						++position;
						break;
					}
					plan->image_capacity_ = plan->image_->bytes()->storage_capacity();
					position = 0;
					phase = Phase::Capacity;
					break;
				case Phase::Capacity:
					if (position < plan->image_->segments().size()) {
						if (!grant(sizeof(BuiltSegment), budget))
							return;
						plan->image_capacity_ +=
							plan->image_->segments()[position++].bytes->storage_capacity();
						break;
					}
					phase = Phase::Seal;
					break;
				case Phase::Seal:
					if (!grant(p::root_bytes + sizeof(PagedStateCounts), budget))
						return;
					plan->root_ = validator->root();
					plan->counts_ = validator->counts();
					plan->descriptor_ = plan->image_->descriptor();
					plan->sealed_ = true;
					phase = Phase::Done;
					break;
				case Phase::Done:
					break;
				}
			}
		} catch (...) {
			failed = true;
			throw;
		}
	}
	bool retire_step() noexcept override {
		ValidationBudget budget{8192, 16};
		if (!retiring) {
			retiring = true;
			charge = 0;
		}
		if (plan && plan->image_) {
			if (!retire_capacity_started) {
				plan->image_capacity_ = plan->image_->bytes()->storage_capacity();
				retire_capacity_started = true;
			}
			while (retire_capacity_index < plan->image_->segments().size()) {
				if (!grant(sizeof(BuiltSegment), budget))
					return false;
				plan->image_capacity_ +=
					plan->image_->segments()[retire_capacity_index++].bytes->storage_capacity();
			}
		}
		if (validator && !validator->discard(budget))
			return false;
		validator.reset();
		artifact_owner.reset();
		if (assembly && !assembly->discard(budget))
			return false;
		assembly.reset();
		if (leaves && !leaves->discard(budget))
			return false;
		leaves.reset();
		if (overlay && !overlay->discard(budget))
			return false;
		overlay.reset();
		base_owner.reset();
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
