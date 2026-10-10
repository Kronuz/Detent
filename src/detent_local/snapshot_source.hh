#pragma once
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/generation.hh"

namespace detent::local::detail {
class PinnedImageSource;
// The recovered generation retains its mapping and lease through encoding.
std::shared_ptr<distribution::SnapshotSource>
	make_snapshot_source(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
std::shared_ptr<distribution::SnapshotSource>
	make_snapshot_source(PinnedImageSource, std::shared_ptr<distribution::StorageLedger>);
} // namespace detent::local::detail
