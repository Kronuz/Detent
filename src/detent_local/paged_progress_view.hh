#pragma once
#include "leased_paged_root.hh"
#include "paged_state_equivalence.hh"
namespace detent::local::detail {

class PagedProgressView {
  public:
	static ReadView take(distribution::RetirementDomain &, distribution::RetirementHandle<LeasedPagedRoot>);
	static ReadView take(distribution::RetirementFactory, std::shared_ptr<distribution::StorageLedger>,
						 distribution::RetirementHandle<LeasedPagedRoot>);
};
} // namespace detent::local::detail
