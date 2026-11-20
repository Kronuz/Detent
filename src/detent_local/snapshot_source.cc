#include "snapshot_source.hh"
#include "paged_snapshot_source.hh"

namespace detent::local::detail {
namespace d = distribution;
std::shared_ptr<d::SnapshotSource> make_snapshot_source(RecoveredGeneration generation,
														std::shared_ptr<d::StorageLedger> account) {
	if (!account)
		throw std::invalid_argument("snapshot source account missing");
	return std::allocate_shared<PagedSnapshotSource>(d::StorageAllocator<PagedSnapshotSource>(account),
													 std::move(generation), account);
}
} // namespace detent::local::detail
