#pragma once
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/generation.hh"

namespace detent::local::detail {
// The recovered generation retains its mapping and lease through encoding.
std::shared_ptr<distribution::SnapshotSource>
	make_snapshot_source(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
} // namespace detent::local::detail
