#pragma once
#include "storage.hh"
#include <memory>

namespace detent::distribution {
// Stateful allocator for component controls and standard-library adapters.
// The actual upstream request, including its header, stays charged even when
// an allocate_shared control block survives its object through weak owners.
template <class T> class StorageAllocator {
	static_assert(alignof(T) <= alignof(std::max_align_t));
	struct alignas(std::max_align_t) Header {
		std::shared_ptr<StorageLedger> account;
		StorageLedger::Ticket ticket;
	};

  public:
	using value_type = T;
	explicit StorageAllocator(std::shared_ptr<StorageLedger> account,
							  PagedStorage::Allocate allocate = std::malloc,
							  PagedStorage::Release release = std::free)
		: account_(std::move(account)), allocate_(allocate), release_(release) {
		if (!account_ || !allocate_ || !release_)
			throw std::invalid_argument("invalid storage allocator");
	}
	template <class U>
	StorageAllocator(const StorageAllocator<U> &other) noexcept
		: account_(other.account_), allocate_(other.allocate_), release_(other.release_) {}
	T *allocate(std::size_t count) {
		if (count > (std::numeric_limits<std::size_t>::max() - sizeof(Header)) / sizeof(T))
			throw std::bad_array_new_length();
		auto ticket = account_->admit(sizeof(Header) + count * sizeof(T));
		auto memory = allocate_(ticket.bytes());
		if (!memory)
			throw std::bad_alloc();
		auto header = new (memory) Header{account_, std::move(ticket)};
		return reinterpret_cast<T *>(header + 1);
	}
	void deallocate(T *pointer, std::size_t) noexcept {
		auto header = reinterpret_cast<Header *>(pointer) - 1;
		auto account = std::move(header->account);
		auto ticket = std::move(header->ticket);
		header->~Header();
		release_(header);
		ticket.reset(); // Refund after the allocation is physically released.
	}
	template <class U> bool operator==(const StorageAllocator<U> &other) const noexcept {
		return account_ == other.account_ && allocate_ == other.allocate_ && release_ == other.release_;
	}

  private:
	std::shared_ptr<StorageLedger> account_;
	PagedStorage::Allocate allocate_;
	PagedStorage::Release release_;
	template <class> friend class StorageAllocator;
};
// Unique controls retain their account and upstream hooks through destruction.
// T may be incomplete in an owning public header; destruction is defined where
// its complete type is available.
template <class T> struct StorageDelete {
	std::shared_ptr<StorageLedger> account;
	PagedStorage::Allocate allocate = std::malloc;
	PagedStorage::Release release = std::free;
	void operator()(T *object) const noexcept {
		StorageAllocator<T> allocator(account, allocate, release);
		std::destroy_at(object);
		allocator.deallocate(object, 1);
	}
};
template <class T> using StorageOwner = std::unique_ptr<T, StorageDelete<T>>;
template <class T, class... Args>
StorageOwner<T> make_storage_owner(std::shared_ptr<StorageLedger> account, PagedStorage::Allocate allocate,
								   PagedStorage::Release release, Args &&...args) {
	StorageAllocator<T> allocator(account, allocate, release);
	auto object = allocator.allocate(1);
	try {
		std::construct_at(object, std::forward<Args>(args)...);
	} catch (...) {
		allocator.deallocate(object, 1);
		throw;
	}
	return StorageOwner<T>(object, StorageDelete<T>{std::move(account), allocate, release});
}
} // namespace detent::distribution
