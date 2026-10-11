#pragma once
#include "leased_paged_root.hh"
#include "paged_state_equivalence.hh"
namespace detent::local::detail {

class PagedProgressView {
  public:
	static ReadView take(distribution::RetirementDomain &, distribution::RetirementHandle<LeasedPagedRoot>);
};
} // namespace detent::local::detail
