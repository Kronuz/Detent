#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace detent::distribution {
class StoragePressure final : public std::length_error {
  public:
	explicit StoragePressure(const char *message) : std::length_error(message) {}
};

// Concurrent admission ledger. A ticket follows an allocation, not the
// facade that initiated it. The ledger must outlive every outstanding ticket.
class StorageLedger {
  public:
	class Ticket {
	  public:
		Ticket() = default;
		Ticket(const Ticket &) = delete;
		Ticket &operator=(const Ticket &) = delete;
		Ticket(Ticket &&other) noexcept { swap(other); }
		Ticket &operator=(Ticket &&other) noexcept {
			if (this != &other) {
				reset();
				swap(other);
			}
			return *this;
		}
		~Ticket() { reset(); }
		std::size_t bytes() const noexcept { return bytes_; }
		void reset() noexcept {
			for (auto ledger = ledger_; ledger; ledger = ledger->parent_.get())
				ledger->used_.fetch_sub(bytes_, std::memory_order_relaxed);
			ledger_ = nullptr;
			bytes_ = 0;
		}

	  private:
		Ticket(StorageLedger &ledger, std::size_t bytes) : ledger_(&ledger), bytes_(bytes) {}
		void swap(Ticket &other) noexcept {
			std::swap(ledger_, other.ledger_);
			std::swap(bytes_, other.bytes_);
		}
		StorageLedger *ledger_ = nullptr;
		std::size_t bytes_ = 0;
		friend class StorageLedger;
	};

	static constexpr std::size_t maximum_depth = 8;
	explicit StorageLedger(std::size_t limit) : limit_(limit) {}
	StorageLedger(std::shared_ptr<StorageLedger> parent, std::size_t limit)
		: limit_(limit), parent_(std::move(parent)) {
		if (!parent_ || limit > parent_->limit() || parent_->depth_ == maximum_depth)
			throw std::invalid_argument("invalid distribution storage hierarchy");
		depth_ = parent_->depth_ + 1;
	}
	StorageLedger(const StorageLedger &) = delete;
	StorageLedger &operator=(const StorageLedger &) = delete;
	Ticket admit(std::size_t bytes) {
		std::array<std::size_t, maximum_depth> observations;
		std::size_t count = 0;
		for (auto ledger = this; ledger; ledger = ledger->parent_.get()) {
			auto used = ledger->used_.load(std::memory_order_relaxed);
			for (;;) {
				if (bytes > ledger->limit_ - used) {
					auto rollback = this;
					for (std::size_t i = 0; i < count; ++i) {
						rollback->used_.fetch_sub(bytes, std::memory_order_relaxed);
						rollback = rollback->parent_.get();
					}
					throw StoragePressure("distribution storage pressure");
				}
				if (ledger->used_.compare_exchange_weak(used, used + bytes, std::memory_order_relaxed))
					break;
			}
			observations[count++] = used + bytes;
		}
		auto ledger = this;
		for (std::size_t i = 0; i < count; ++i) {
			auto peak = ledger->peak_.load(std::memory_order_relaxed);
			while (peak < observations[i] &&
				   !ledger->peak_.compare_exchange_weak(peak, observations[i], std::memory_order_relaxed)) {
			}
			ledger = ledger->parent_.get();
		}
		return Ticket(*this, bytes);
	}
	std::size_t used() const noexcept { return used_.load(std::memory_order_relaxed); }
	std::size_t peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
	std::size_t limit() const noexcept { return limit_; }

  private:
	const std::size_t limit_;
	const std::shared_ptr<StorageLedger> parent_;
	std::size_t depth_ = 1;
	std::atomic<std::size_t> used_{0}, peak_{0};
};

// Single-owner stable storage with intrusive page metadata. All component-owned
// page capacity, padding, and headers are admitted before upstream allocation.
// malloc overhead and process RSS are deliberately outside this capacity metric.
// Disposal can release one page per scheduler turn, including after pressure.
class PagedStorage {
  public:
	using Allocate = void *(*)(std::size_t);
	using Release = void (*)(void *);
	explicit PagedStorage(StorageLedger &ledger, std::size_t page_bytes = 65536,
						  Allocate allocate = std::malloc, Release release = std::free,
						  std::size_t capacity_limit = std::numeric_limits<std::size_t>::max())
		: ledger_(&ledger), page_bytes_(page_bytes), allocate_(allocate), release_(release),
		  limit_(capacity_limit) {
		if (!page_bytes || !allocate || !release)
			throw std::invalid_argument("invalid distribution storage configuration");
	}
	PagedStorage(const PagedStorage &) = delete;
	PagedStorage &operator=(const PagedStorage &) = delete;
	PagedStorage(PagedStorage &&other) noexcept
		: ledger_(other.ledger_), page_bytes_(other.page_bytes_), allocate_(other.allocate_),
		  release_(other.release_), first_(std::exchange(other.first_, nullptr)),
		  disposing_(other.disposing_), limit_(other.limit_), used_(std::exchange(other.used_, 0)),
		  peak_(other.peak_) {}
	PagedStorage &operator=(PagedStorage &&) = delete;
	~PagedStorage() {
		while (!release_one()) {
		}
	}

	void *allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
		if (disposing_)
			throw std::logic_error("distribution storage is retiring");
		if (!alignment || (alignment & (alignment - 1)) || alignment > alignof(std::max_align_t))
			throw std::invalid_argument("unsupported distribution storage alignment");
		if (!bytes)
			bytes = 1;
		if (first_) {
			const auto padding = (-first_->used) & (alignment - 1);
			if (padding <= first_->capacity - first_->used &&
				bytes <= first_->capacity - first_->used - padding) {
				first_->used += padding;
				auto result = data(first_) + first_->used;
				first_->used += bytes;
				return result;
			}
		}
		if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(Page))
			throw std::length_error("distribution storage extent overflow");
		const auto capacity = std::max(page_bytes_, bytes);
		if (capacity > std::numeric_limits<std::size_t>::max() - sizeof(Page))
			throw std::length_error("distribution storage page overflow");
		if (sizeof(Page) + capacity > limit_ - used_)
			throw StoragePressure("distribution arena pressure");
		auto ticket = ledger_->admit(sizeof(Page) + capacity);
		void *memory = allocate_(ticket.bytes());
		if (!memory)
			throw std::bad_alloc(); // ticket rolls admission back
		auto page = new (memory) Page{first_, std::move(ticket), capacity, bytes};
		first_ = page;
		used_ += sizeof(Page) + capacity;
		peak_ = std::max(peak_, used_);
		return data(page);
	}
	// True means disposal is complete. Remaining references must not be used
	// once disposal begins. Releasing a page refunds only its actual ownership.
	bool release_one() noexcept {
		disposing_ = true;
		if (!first_)
			return true;
		auto page = first_;
		first_ = page->next;
		auto ticket = std::move(page->ticket);
		page->~Page();
		release_(page);
		used_ -= ticket.bytes();
		ticket.reset();
		return !first_;
	}
	std::size_t front_capacity() const noexcept { return first_ ? first_->ticket.bytes() : 0; }
	std::size_t used() const noexcept { return used_; }
	std::size_t peak() const noexcept { return peak_; }
	static constexpr std::size_t page_overhead() noexcept;

  private:
	struct alignas(std::max_align_t) Page {
		Page *next;
		StorageLedger::Ticket ticket;
		std::size_t capacity, used;
	};
	static std::byte *data(Page *page) noexcept { return reinterpret_cast<std::byte *>(page + 1); }
	StorageLedger *ledger_;
	std::size_t page_bytes_;
	Allocate allocate_;
	Release release_;
	Page *first_ = nullptr;
	bool disposing_ = false;
	std::size_t limit_, used_ = 0, peak_ = 0;
};
inline constexpr std::size_t PagedStorage::page_overhead() noexcept { return sizeof(Page); }

} // namespace detent::distribution
