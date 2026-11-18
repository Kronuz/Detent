#pragma once
#include "paged_graph_validator.hh"
#include "recovery_context.hh"
#include "segment_registry.hh"

namespace detent::local::detail {
struct TrustedGeneration {
	distribution::RetirementHandle<SegmentRegistry> registry;
	PagedStateCounts counts;
};
// Builds a generation's registry from its `.read` and `.live` records, mapping each pack without reading
// or hashing it. Throws when either record is missing or wrong, which refuses the store.
//
// A cold start maps every pack itself and gates its reads on first touch. The recovery that follows this
// process's own publication (`admission`) takes the packs the plan reserved, fills the plan's registry and
// leaves the packs it just built ungated; a `warm` prior supplies its own mappings, and their first-touch
// state, for the segments it holds.
struct TrustedOwn {
	MappedRecoveryAdmission *admission = nullptr;
	const SegmentRegistry *prior = nullptr;
};
class TrustedRestart {
  public:
	static TrustedGeneration
	open(int directory, uid_t owner, const Descriptor &, const paged::Root &, paged::Limits bounds,
		 const ViewLimits &limits, distribution::RetirementFactory,
		 std::shared_ptr<distribution::StorageLedger> heap, std::shared_ptr<distribution::StorageLedger> mappings,
		 RecoveryMetrics *, TrustedOwn = {});
};
} // namespace detent::local::detail
