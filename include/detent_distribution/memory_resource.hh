#pragma once
#include "allocator.hh"
#include <allocators/memory_resource.h>
namespace detent::distribution {
using LedgerMemoryResource = allocators::ChargedMemoryResource;
inline std::shared_ptr<std::pmr::memory_resource>
make_storage_resource(std::shared_ptr<StorageLedger> account, PagedStorage::Allocate allocate = std::malloc,
                      PagedStorage::Release release = std::free) {
    return allocators::make_charged_resource(std::move(account), allocate, release);
}
} // namespace detent::distribution
