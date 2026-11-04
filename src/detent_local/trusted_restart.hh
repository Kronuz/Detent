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
class TrustedRestart {
  public:
	static TrustedGeneration
	open(int directory, uid_t owner, const Descriptor &, const paged::Root &, StorageIdentity storage,
		 paged::Limits bounds, const ViewLimits &limits, distribution::RetirementFactory,
		 std::shared_ptr<distribution::StorageLedger> heap, std::shared_ptr<distribution::StorageLedger> mappings,
		 RecoveryMetrics *);
};
} // namespace detent::local::detail
