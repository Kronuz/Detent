#pragma once
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/generation.hh"

namespace detent::local::detail {
// The recovered generation retains its mapping and lease through encoding. The v1 encoding names
// `identity` (its revision is ignored), which must map to the generation's cursor (D88).
std::shared_ptr<distribution::SnapshotSource>
	make_snapshot_source(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>,
						 const distribution::Cursor &identity);
} // namespace detent::local::detail
