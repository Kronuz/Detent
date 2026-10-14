#include "sparse_plan_finalizer.hh"
#include "paged_navigation.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
SparsePlanFinalizer::SparsePlanFinalizer(d::RetirementHandle<PagedPlanState> state,
										 std::shared_ptr<d::StorageLedger> heap, p::Limits bounds,
										 std::uint64_t maximum, std::optional<p::Digest> expected)
	: plan(std::move(state)), account(std::move(heap)),
	  old_live(d::StorageAllocator<SegmentReachability>(account)),
	  added(d::StorageAllocator<SegmentInput>(account)), limits(bounds), maximum_output_bytes(maximum),
	  expected_logical(expected) {
	old_live.reserve(plan->registry_->inputs().size());
}
void SparsePlanFinalizer::start(TrieOverlay &&source) {
	if (overlay || retiring)
		throw std::logic_error("invalid sparse finalization handoff");
	(void)source.root();
	overlay.emplace(std::move(source));
}
bool SparsePlanFinalizer::done() const noexcept { return overlay && !failed && phase == Phase::Done; }
bool SparsePlanFinalizer::grant(std::size_t bytes, ValidationBudget &budget) {
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
void SparsePlanFinalizer::step(ValidationBudget &budget) {
	if (failed || !overlay || retiring)
		throw std::logic_error("failed sparse plan finalization");
	try {
		while (phase != Phase::Done) {
			switch (phase) {
			case Phase::Reachability:
				if (position < plan->registry_->inputs().size()) {
					if (!grant(sizeof(SegmentReachability), budget))
						return;
					old_live.push_back(plan->registry_->reachability(position++));
					break;
				}
				assembly.emplace(account, *overlay, plan->registry_->inputs(), old_live, plan->base_->root(),
								 plan->base_->storage_identity(), plan->descriptor_.generation,
								 plan->descriptor_.cursor, limits, maximum_output_bytes);
				position = 0;
				phase = Phase::Assemble;
				break;
			case Phase::Assemble:
				assembly->step(budget);
				if (!assembly->done())
					return;
				plan->image_.emplace(assembly->take());
				if (expected_logical && plan->image_->root().logical != *expected_logical)
					throw std::invalid_argument("compaction changed logical state or exceeded output bound");
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
					artifact_owner = std::allocate_shared<State>(d::StorageAllocator<State>(account), plan);
					validator.emplace(account, plan->base_, artifact_owner, added, plan->image_->root(),
									  limits);
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
					plan->base_live_.push_back(
						{old_live[position].pages - removed.pages, old_live[position].bytes - removed.bytes});
					++position;
					break;
				}
				if (position < old_live.size() + added.size()) {
					if (!grant(sizeof(SegmentReachability), budget))
						return;
					const auto i = position++ - old_live.size();
					const auto sorted = find_segment(added, plan->image_->segments()[i].ownership.segment);
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
					const auto &segment = plan->image_->segments()[position++];
					plan->image_capacity_ += segment.bytes->storage_capacity();
					if (segment.ownership.length > maximum_output_bytes - output_bytes)
						throw std::length_error("compaction output bound exceeded");
					output_bytes += segment.ownership.length;
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
bool SparsePlanFinalizer::discard(ValidationBudget &budget) {
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
	if (overlay && !overlay->discard(budget))
		return false;
	overlay.reset();
	plan.reset();
	return true;
}
} // namespace detent::local::detail
