#pragma once
#include "detent_distribution/storage.hh"
#include <array>
#include <iterator>
#include <memory>
#include <string_view>
#include <type_traits>

namespace detent::distribution {

// A bounded-depth directory replaces a growing contiguous pointer index.
// Directory nodes and row blocks use the same admitted storage as text.
// Rows never move; removing rows does not refund their containing pages.
template <class T> class StableRows {
	static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
	static_assert(alignof(T) <= alignof(std::max_align_t));
	static constexpr std::size_t width = 256, block_rows = 128;
	struct Block {
		T rows[block_rows];
	};
	struct Leaf {
		std::array<Block *, width> blocks{};
	};
	struct Middle {
		std::array<Leaf *, width> leaves{};
	};
	struct Root {
		std::array<Middle *, width> middles{};
	};
	static constexpr std::uint64_t maximum = std::uint64_t(width) * width * width * block_rows;

  public:
	explicit StableRows(PagedStorage &storage) : storage_(&storage) {}
	StableRows(const StableRows &) = delete;
	StableRows &operator=(const StableRows &) = delete;
	StableRows(StableRows &&other) noexcept
		: storage_(other.storage_), root_(std::exchange(other.root_, nullptr)),
		  size_(std::exchange(other.size_, 0)) {}
	StableRows &operator=(StableRows &&) = delete;
	std::uint64_t size() const noexcept { return size_; }
	bool empty() const noexcept { return !size_; }
	void push_back(const T &value) {
		if (size_ == maximum)
			throw std::length_error("distribution row index exhausted");
		if (!root_)
			root_ = node<Root>();
		auto index = size_ / block_rows;
		auto &middle = root_->middles[index / (width * width)];
		if (!middle)
			middle = node<Middle>();
		auto &leaf = middle->leaves[(index / width) % width];
		if (!leaf)
			leaf = node<Leaf>();
		auto &block = leaf->blocks[index % width];
		if (!block)
			block = node<Block>();
		block->rows[size_ % block_rows] = value;
		++size_;
	}
	const T &operator[](std::uint64_t index) const {
		if (index >= size_)
			throw std::out_of_range("distribution row index");
		auto block = index / block_rows;
		return root_->middles[block / (width * width)]
			->leaves[(block / width) % width]
			->blocks[block % width]
			->rows[index % block_rows];
	}
	T &operator[](std::uint64_t index) { return const_cast<T &>(std::as_const(*this)[index]); }
	const T &back() const { return (*this)[size_ ? size_ - 1 : size_]; }
	T &back() { return (*this)[size_ ? size_ - 1 : size_]; }
	void clear() noexcept { size_ = 0; }
	void pop_back() {
		if (!size_)
			throw std::out_of_range("empty distribution rows");
		--size_;
	}
	class Iterator {
	  public:
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;
		using reference = const T &;
		using pointer = const T *;
		Iterator() = default;
		const T &operator*() const { return (*rows_)[index_]; }
		const T *operator->() const { return &**this; }
		Iterator &operator++() {
			++index_;
			return *this;
		}
		Iterator operator++(int) {
			auto previous = *this;
			++*this;
			return previous;
		}
		bool operator==(const Iterator &) const = default;

	  private:
		Iterator(const StableRows *rows, std::uint64_t index) : rows_(rows), index_(index) {}
		const StableRows *rows_ = nullptr;
		std::uint64_t index_ = 0;
		friend class StableRows;
	};
	Iterator begin() const { return Iterator(this, 0); }
	Iterator end() const { return Iterator(this, size_); }

  private:
	template <class Node> Node *node() {
		static_assert(std::is_nothrow_default_constructible_v<Node>);
		return std::construct_at(static_cast<Node *>(storage_->allocate(sizeof(Node), alignof(Node))));
	}
	PagedStorage *storage_;
	Root *root_ = nullptr;
	std::uint64_t size_ = 0;
};

struct TextRef {
	std::string_view value;
	std::size_t size() const noexcept { return value.size(); }
	operator std::string_view() const noexcept { return value; }
};
inline TextRef store_text(PagedStorage &storage, std::string_view text) {
	if (text.empty())
		return {};
	auto bytes = static_cast<char *>(storage.allocate(text.size(), 1));
	std::copy(text.begin(), text.end(), bytes);
	return {{bytes, text.size()}};
}

} // namespace detent::distribution
