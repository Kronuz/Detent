#pragma once
#include "leased_paged_root.hh"

namespace detent::local::detail {
// The recovered paged registry whose packs the agent's scrub hashes; the handle pins the mappings.
struct ScrubSource {
	static distribution::RetirementHandle<LeasedPagedRoot> root(const RecoveredGeneration &generation) {
		return generation.paged_;
	}
};
} // namespace detent::local::detail
