#pragma once
#include "paged_warm_validator.hh"
namespace detent::local::detail {
// Builds independent target ownership only after the contextual proof succeeds.
class WarmRegistryMerge {
  public:
	WarmRegistryMerge(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementFactory,
					  distribution::RetirementHandle<LeasedPagedRoot>,
					  distribution::RetirementHandle<SegmentRegistry>, WarmGraphValidator &,
					  paged::Limits = {}, distribution::RetirementHandle<SegmentRegistry> admitted = {});
	void step(ValidationBudget &);
	bool done() const noexcept;
	distribution::RetirementHandle<SegmentRegistry> take();

  private:
	distribution::RetirementHandle<SegmentRegistry> base_, fresh_, target_;
	WarmGraphValidator &validator_;
	std::size_t old_ = 0, added_ = 0, charge_ = 0;
	bool sealed_ = false, complete_ = false, failed_ = false;
};
} // namespace detent::local::detail
