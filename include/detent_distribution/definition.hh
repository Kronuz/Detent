#pragma once
#include "detent/model.hh"
#include "rows.hh"

namespace detent::distribution {
struct RowRange {
	std::uint64_t start = 0, count = 0;
};
struct OverrideRow {
	std::string_view first;
	GroupOverride second;
};
// Borrowed immutable range. Keep the owning validated update alive while
// retaining a definition or its override references.
class OverrideRange {
  public:
	class const_iterator {
	  public:
		using value_type = OverrideRow;
		using reference = const OverrideRow &;
		using pointer = const OverrideRow *;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;
		const_iterator() = default;
		reference operator*() const { return (*rows_)[index_]; }
		pointer operator->() const { return &**this; }
		const_iterator &operator++() {
			++index_;
			return *this;
		}
		const_iterator operator++(int) {
			auto old = *this;
			++*this;
			return old;
		}
		bool operator==(const const_iterator &) const = default;

	  private:
		const_iterator(const StableRows<OverrideRow> *rows, std::uint64_t index)
			: rows_(rows), index_(index) {}
		const StableRows<OverrideRow> *rows_ = nullptr;
		std::uint64_t index_ = 0;
		friend class OverrideRange;
	};
	OverrideRange() = default;
	OverrideRange(const StableRows<OverrideRow> &rows, std::uint64_t start, std::uint64_t count)
		: rows_(&rows), start_(start), count_(count) {}
	std::uint64_t size() const noexcept { return count_; }
	bool empty() const noexcept { return !count_; }
	const OverrideRow &operator[](std::uint64_t ordinal) const {
		if (ordinal >= count_)
			throw std::out_of_range("distribution override ordinal");
		return (*rows_)[start_ + ordinal];
	}
	const_iterator begin() const { return {rows_, start_}; }
	const_iterator end() const { return {rows_, start_ + count_}; }
	const OverrideRow &back() const {
		if (!count_)
			throw std::out_of_range("empty distribution overrides");
		return (*rows_)[start_ + count_ - 1];
	}
	const GroupOverride &at(std::string_view key) const {
		std::uint64_t low = 0, high = count_;
		while (low < high) {
			auto middle = low + (high - low) / 2;
			if ((*rows_)[start_ + middle].first < key)
				low = middle + 1;
			else
				high = middle;
		}
		if (low == count_ || (*rows_)[start_ + low].first != key)
			throw std::out_of_range("distribution override not found");
		return (*rows_)[start_ + low].second;
	}

  private:
	const StableRows<OverrideRow> *rows_ = nullptr;
	std::uint64_t start_ = 0, count_ = 0;
};
struct WireBoolDef {
	bool default_value = false;
	RowRange overrides;
	std::optional<std::uint8_t> rollout_pct;
};
using WireDefinition = std::variant<WireBoolDef, IntDef>;
} // namespace detent::distribution
