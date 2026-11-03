#pragma once
#include <allocators/storage.h>
namespace detent::distribution {
using StoragePressure = allocators::ResourcePressure;
using StorageLedger = allocators::ByteAccount;
using PagedStorage = allocators::PagedStorage;
} // namespace detent::distribution
