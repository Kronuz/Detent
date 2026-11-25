#pragma once
#include <allocators/storage.h>

// Detent's names for allocators' byte accounting; allocators holds the one implementation.
namespace detent::distribution {
using StoragePressure = allocators::ResourcePressure;
using StorageLedger = allocators::ByteAccount;
using PagedStorage = allocators::PagedStorage;
} // namespace detent::distribution
