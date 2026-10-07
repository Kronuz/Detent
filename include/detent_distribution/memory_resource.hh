#pragma once
#include "allocator.hh"
#include <bit>
#include <memory_resource>

namespace detent::distribution {
// An unpooled PMR adapter. Full upstream requests, including alignment slack,
// remain charged until the original allocation is actually released.
class LedgerMemoryResource final : public std::pmr::memory_resource {
	struct alignas(std::max_align_t) Header {
		std::shared_ptr<StorageLedger> account;
		StorageLedger::Ticket ticket;
	};

  public:
	explicit LedgerMemoryResource(std::shared_ptr<StorageLedger> account,
								  PagedStorage::Allocate allocate = std::malloc,
								  PagedStorage::Release release = std::free)
		: account_(std::move(account)), allocate_(allocate), release_(release) {
		if (!account_ || !allocate_ || !release_)
			throw std::invalid_argument("invalid ledger memory resource");
	}

  private:
	void *do_allocate(std::size_t bytes, std::size_t alignment) override {
		if (!std::has_single_bit(alignment))
			throw std::bad_alloc();
		alignment = std::max(alignment, alignof(Header *));
		constexpr auto overhead = sizeof(Header) + sizeof(Header *);
		const auto padding = alignment - 1;
		const auto maximum = std::numeric_limits<std::size_t>::max();
		if (bytes > maximum - overhead || padding > maximum - overhead - bytes)
			throw std::bad_alloc();
		auto ticket = account_->admit(overhead + bytes + padding);
		auto memory = allocate_(ticket.bytes());
		if (!memory)
			throw std::bad_alloc();
		auto header = new (memory) Header{account_, std::move(ticket)};
		void *payload = static_cast<char *>(memory) + overhead;
		auto space = bytes + padding;
		if (!std::align(alignment, bytes, payload, space)) {
			dispose(header);
			throw std::bad_alloc();
		}
		std::construct_at(reinterpret_cast<Header **>(payload) - 1, header);
		return payload;
	}
	void do_deallocate(void *pointer, std::size_t, std::size_t) noexcept override {
		dispose(*(reinterpret_cast<Header **>(pointer) - 1));
	}
	void dispose(Header *header) noexcept {
		auto account = std::move(header->account);
		auto ticket = std::move(header->ticket);
		std::destroy_at(header);
		release_(header);
		ticket.reset();
	}
	bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
		return this == &other;
	}
	std::shared_ptr<StorageLedger> account_;
	PagedStorage::Allocate allocate_;
	PagedStorage::Release release_;
};
inline std::shared_ptr<std::pmr::memory_resource>
make_storage_resource(std::shared_ptr<StorageLedger> account, PagedStorage::Allocate allocate = std::malloc,
					  PagedStorage::Release release = std::free) {
	return std::allocate_shared<LedgerMemoryResource>(
		StorageAllocator<LedgerMemoryResource>(account, allocate, release), account, allocate, release);
}
} // namespace detent::distribution
