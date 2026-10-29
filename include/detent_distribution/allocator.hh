#pragma once
#include "storage.hh"
#include <allocators/charged_allocator.h>
namespace detent::distribution {
template <class T> using StorageAllocator = allocators::ChargedAllocator<T>;
template <class T> using StorageDelete = allocators::ChargedDelete<T>;
template <class T> using StorageOwner = allocators::ChargedOwner<T>;
using allocators::make_charged_owner;
template <class T, class... Args>
StorageOwner<T> make_storage_owner(std::shared_ptr<StorageLedger> account, PagedStorage::Allocate allocate,
                                  PagedStorage::Release release, Args &&...args) {
    return make_charged_owner<T>(std::move(account), allocate, release, std::forward<Args>(args)...);
}
} // namespace detent::distribution
