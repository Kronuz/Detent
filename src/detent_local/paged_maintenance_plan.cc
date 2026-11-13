#include "detent_local/derived_ids.hh"
#include "paged_limits.hh"
#include "paged_maintenance_plan.hh"
namespace detent::local::detail {
PagedMaintenanceBuilder::PagedMaintenanceBuilder(RecoveredGeneration base,
												 distribution::RetirementFactory factory,
												 std::shared_ptr<distribution::StorageLedger> heap, ViewLimits limits)
	: base_(std::move(base)), factory_(std::move(factory)), heap_(std::move(heap)), limits_(limits) {
	if (!heap_ || !factory_.uses_account(heap_))
		throw std::invalid_argument("maintenance requires exact resources");
	limits_.retirement_domain = nullptr;
	if (limits_.bytes < paged::root_bytes)
		throw std::invalid_argument("maintenance manifest exceeds limit");
	const auto bounds = paged_limits(limits_, paged::root_bytes);
	// A full copy at the base's own cursor: a zero base and the base's ordinal + 1 (D8.2).
	const auto &head = base_.descriptor();
	encoder_.emplace(base_, heap_, next_generation(head, head.cursor, head.profile, true).id, bounds);
}
bool PagedMaintenanceBuilder::grant(std::size_t amount, ValidationBudget &budget) {
	if (!budget.rows)
		return false;
	if (!charge_)
		charge_ = amount;
	const auto bytes = std::min(charge_, budget.bytes);
	charge_ -= bytes;
	budget.bytes -= bytes;
	if (charge_)
		return false;
	--budget.rows;
	return true;
}
void PagedMaintenanceBuilder::step(ValidationBudget &budget) {
	if (failed_ || sealed_ && !state_)
		throw std::logic_error("inactive maintenance builder");
	if (sealed_)
		return;
	try {
		if (encoder_) {
			encoder_->step(budget);
			if (!encoder_->done())
				return;
			validator_.emplace(encoder_->take(), factory_, heap_, limits_);
			encoder_.reset();
		}
		if (!state_) {
			validator_->step(budget);
			if (!validator_->done())
				return;
			state_ = factory_.make<PagedPlanState>(heap_, base_, validator_->image());
			state_->counts_ = validator_->counts();
		}
		const auto segments = state_->replacement_->image().segments();
		while (copied_ < segments.size()) {
			if (!grant(sizeof(SegmentReachability), budget))
				return;
			state_->new_live_.push_back(
				validator_->segment_reachability(segments[copied_++].ownership.segment));
		}
		while (histogram_ < state_->histogram_.size()) {
			if (!grant(sizeof(std::uint32_t), budget))
				return;
			state_->histogram_[histogram_] = validator_->override_histogram()[histogram_];
			++histogram_;
		}
		if (!equality_)
			equality_.emplace(StateCursor(base_, heap_), StateCursor(validator_->take(), heap_));
		equality_->step(budget);
		if (!equality_->done())
			return;
		if (!equality_->equal())
			throw std::invalid_argument("maintenance changes logical state");
		auto proof = equality_->take_proof();
		if (!proof.binds_construction(base_, state_->descriptor_))
			throw std::invalid_argument("maintenance equality proof binding mismatch");
		state_->maintenance_proof_.emplace(std::move(proof));
		state_->sealed_ = true;
		sealed_ = true;
		validator_.reset();
		equality_.reset();
	} catch (...) {
		failed_ = true;
		throw;
	}
}
bool PagedMaintenanceBuilder::done() const noexcept { return sealed_ && !failed_; }
PagedPlan PagedMaintenanceBuilder::take() {
	if (!done() || !state_)
		throw std::logic_error("maintenance plan is unavailable");
	return PagedPlan(std::move(state_));
}
std::shared_ptr<const StateEquivalenceProof> PagedMaintenanceBuilder::evidence() const {
	if (!done() || !state_ || !state_->maintenance_proof_)
		throw std::logic_error("maintenance evidence unavailable");
	using Handle = distribution::RetirementHandle<PagedPlanState>;
	auto owner = std::allocate_shared<Handle>(distribution::StorageAllocator<Handle>(heap_), state_);
	return {std::move(owner), &*state_->maintenance_proof_};
}
} // namespace detent::local::detail
