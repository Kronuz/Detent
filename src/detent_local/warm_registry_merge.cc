#include "warm_registry_merge.hh"
#include <algorithm>
#include <cstring>
namespace detent::local::detail {
WarmRegistryMerge::WarmRegistryMerge(std::shared_ptr<distribution::StorageLedger> account,
									 distribution::RetirementFactory factory,
									 distribution::RetirementHandle<LeasedPagedRoot> base,
									 distribution::RetirementHandle<SegmentRegistry> fresh,
									 WarmGraphValidator &validator, paged::Limits limits,
									 distribution::RetirementHandle<SegmentRegistry> admitted)
	: base_(base ? base->registry() : distribution::RetirementHandle<SegmentRegistry>{}),
	  fresh_(std::move(fresh)), validator_(validator) {
	if (!base || !fresh_ || !validator.done() || !base_->uses_account(account) ||
		!fresh_->uses_account(account))
		throw std::invalid_argument("warm registry merge requires completed exact-account proof");
	limits.segments = validator.root().segments;
	if (admitted && (!admitted->uses_account(account) || admitted->physical_bytes()))
		throw std::invalid_argument("invalid staged warm registry");
	target_ = admitted ? std::move(admitted)
					   : factory.make<SegmentRegistry>(account, base->storage_identity(), limits);
}
void WarmRegistryMerge::step(ValidationBudget &budget) {
	if (failed_ || !target_)
		throw std::logic_error("warm registry merge unavailable");
	try {
		const auto base = base_->inputs(), fresh = fresh_->inputs();
		while (old_ < base.size() || added_ < fresh.size()) {
			if (!budget.rows)
				return;
			if (!charge_)
				charge_ = sizeof(SegmentInput) + sizeof(SegmentReachability) +
						  sizeof(distribution::RetirementHandle<SegmentMapping>);
			const auto paid = std::min(charge_, budget.bytes);
			charge_ -= paid;
			budget.bytes -= paid;
			if (charge_)
				return;
			--budget.rows;
			if (old_ < base.size() && base_->reachability(old_).pages == validator_.removed(old_).pages) {
				++old_;
				continue;
			}
			const bool take_old =
				old_ < base.size() &&
				(added_ == fresh.size() || std::memcmp(base[old_].ownership.segment.data(),
													   fresh[added_].ownership.segment.data(), 16) < 0);
			target_->append(take_old ? base_->mapping(old_++) : fresh_->mapping(added_++));
		}
		if (!sealed_) {
			if (!budget.rows)
				return;
			--budget.rows;
			target_->seal();
			sealed_ = true;
		}
		complete_ = validator_.record_reachability(*target_, budget);
	} catch (...) {
		failed_ = true;
		throw;
	}
}
bool WarmRegistryMerge::done() const noexcept { return complete_ && !failed_ && bool(target_); }
distribution::RetirementHandle<SegmentRegistry> WarmRegistryMerge::take() {
	if (!done())
		throw std::logic_error("warm registry merge incomplete");
	return std::move(target_);
}
} // namespace detent::local::detail
