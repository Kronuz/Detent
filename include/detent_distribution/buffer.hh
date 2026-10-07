#pragma once
#include "storage.hh"
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

namespace detent::distribution {
// Fixed-capacity, single-writer bytes. Full backing capacity is admitted before
// allocation; exposing initialized bytes does not refund unused capacity.
class OwnedBuffer {
  public:
	OwnedBuffer(std::shared_ptr<StorageLedger> account, std::size_t capacity)
		: account_(std::move(account)), arena_(checked_account(), 1), capacity_(capacity) {
		if (capacity)
			data_ = static_cast<char *>(arena_.allocate(capacity, 1));
	}
	OwnedBuffer(const OwnedBuffer &) = delete;
	OwnedBuffer &operator=(const OwnedBuffer &) = delete;
	void resize(std::size_t size) {
		if (sealed_ || size < size_ || size > capacity_)
			throw std::logic_error("invalid owned buffer growth");
		if (size != size_)
			std::fill(data_ + size_, data_ + size, '\0');
		size_ = size;
	}
	void append(std::string_view bytes) {
		if (sealed_ || bytes.size() > capacity_ - size_)
			throw std::logic_error("invalid owned buffer append");
		if (!bytes.empty())
			std::memmove(data_ + size_, bytes.data(), bytes.size());
		size_ += bytes.size();
	}
	std::span<char> mutable_bytes() {
		if (sealed_)
			throw std::logic_error("owned buffer sealed");
		return {data_, size_};
	}
	void seal() {
		if (size_ != capacity_)
			throw std::logic_error("owned buffer incomplete");
		sealed_ = true;
	}
	std::size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return !size_; }
	const char *data() const noexcept { return data_ ? data_ : ""; }
	const char *begin() const noexcept { return data(); }
	const char *end() const noexcept { return data() + size_; }
	std::string_view view() const noexcept { return {data(), size_}; }
	operator std::string_view() const noexcept { return view(); }
	std::size_t storage_capacity() const noexcept { return arena_.used(); }
	const std::shared_ptr<StorageLedger> &account() const noexcept { return account_; }

  private:
	StorageLedger &checked_account() const {
		if (!account_)
			throw std::invalid_argument("owned buffer account missing");
		return *account_;
	}
	std::shared_ptr<StorageLedger> account_;
	PagedStorage arena_;
	char *data_ = nullptr;
	std::size_t capacity_ = 0, size_ = 0;
	bool sealed_ = false;
};
} // namespace detent::distribution
