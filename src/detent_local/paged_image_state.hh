#pragma once
#include "paged_snapshot_builder.hh"
#include <vector>
namespace detent::local::detail {
struct PagedImage::Impl {
	paged::Root root;
	Descriptor descriptor;
	std::shared_ptr<const distribution::OwnedBuffer> bytes;
	std::vector<BuiltSegment, distribution::StorageAllocator<BuiltSegment>> segments;
	bool sealed = false, discarding = false;
	Impl(std::shared_ptr<distribution::StorageLedger> account, std::size_t capacity)
		: segments(distribution::StorageAllocator<BuiltSegment>(std::move(account))) {
		segments.reserve(capacity);
	}
};
} // namespace detent::local::detail
