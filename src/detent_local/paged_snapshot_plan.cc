#include "paged_limits.hh"
#include "paged_snapshot_plan.hh"
namespace detent::local::detail {
namespace d = distribution;
PagedSnapshotPlanBuilder::PagedSnapshotPlanBuilder(std::shared_ptr<const d::ValidatedSnapshot> source,
												   std::optional<RecoveredGeneration> base,
												   DirectoryIdentity directory, StorageIdentity storage,
												   GenerationId generation, d::RetirementFactory factory,
												   std::shared_ptr<d::StorageLedger> heap, ViewLimits limits)
	: base_(std::move(base)), factory_(std::move(factory)), heap_(std::move(heap)), directory_(directory),
	  storage_(storage), limits_(limits) {
	if (!source || source->storage_account() != heap_ || !factory_.uses_account(heap_) ||
		(base_ &&
		 (base_->directory_identity() != directory_ || base_->descriptor().generation == generation)) ||
		limits_.bytes < paged::root_bytes)
		throw std::invalid_argument("snapshot plan requires actual bindings and exact resources");
	limits_.retirement_domain = nullptr;
	const auto bounds = paged_limits(limits_, paged::root_bytes);
	encoder_.emplace(std::move(source), storage_, generation, bounds);
}
bool PagedSnapshotPlanBuilder::grant(std::size_t amount, ValidationBudget &budget) {
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
void PagedSnapshotPlanBuilder::step(ValidationBudget &budget) {
	if (failed_ || finished_ && !state_)
		throw std::logic_error("inactive paged snapshot plan builder");
	if (finished_)
		return;
	try {
		if (encoder_) {
			encoder_->step(budget);
			if (!encoder_->done())
				return;
			validator_.emplace(encoder_->take(), factory_, heap_, storage_, limits_);
			encoder_.reset();
		}
		if (!state_) {
			validator_->step(budget);
			if (!validator_->done())
				return;
			const auto &target = validator_->image()->image().descriptor();
			const auto intent = !base_										  ? PagedPlanIntent::Fresh
								: target.cursor == base_->descriptor().cursor ? PagedPlanIntent::Maintenance
								: target.cursor.same_identity(base_->descriptor().cursor)
									? PagedPlanIntent::Forward
									: PagedPlanIntent::TrustedReset;
			state_ = factory_.make<PagedPlanState>(heap_, base_, validator_->image(), storage_, directory_,
												   intent);
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
		if (state_->intent_ == PagedPlanIntent::Maintenance) {
			if (!equality_)
				equality_.emplace(StateCursor(*base_, heap_), StateCursor(validator_->take(), heap_));
			equality_->step(budget);
			if (!equality_->done())
				return;
			if (!equality_->equal())
				throw std::invalid_argument("same-cursor snapshot changes logical state");
			unchanged_proof_.emplace(equality_->take_proof());
			if (!unchanged_proof_->binds_construction(*base_, state_->descriptor_))
				throw std::invalid_argument("unchanged snapshot proof binding mismatch");
		} else
			state_->sealed_ = true;
		finished_ = true;
		validator_.reset();
		equality_.reset();
	} catch (...) {
		failed_ = true;
		throw;
	}
}
bool PagedSnapshotPlanBuilder::done() const noexcept { return finished_ && !failed_; }
bool PagedSnapshotPlanBuilder::unchanged() const {
	if (!done())
		throw std::logic_error("snapshot plan incomplete");
	return state_ && state_->intent_ == PagedPlanIntent::Maintenance;
}
PagedPlan PagedSnapshotPlanBuilder::take() {
	if (!done() || !state_ || unchanged())
		throw std::logic_error("snapshot publication plan unavailable");
	return PagedPlan(std::move(state_));
}
StateEquivalenceProof PagedSnapshotPlanBuilder::take_unchanged_proof() {
	if (!done() || !unchanged_proof_)
		throw std::logic_error("unchanged snapshot proof unavailable");
	auto proof = std::move(*unchanged_proof_);
	unchanged_proof_.reset();
	state_.reset();
	return proof;
}
} // namespace detent::local::detail
