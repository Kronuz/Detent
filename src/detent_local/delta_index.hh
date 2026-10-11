#pragma once
#include "detent_distribution/protocol.hh"
#include "detent_local/view.hh"
namespace detent::local::detail {
// Ordered mutation references preserve transaction order for repeated keys.
class DeltaIndex {
  public:
	struct Entry {
		std::uint64_t ordinal = 0, revision = 0;
	};
	DeltaIndex(distribution::PagedStorage &arena, const distribution::ValidatedDelta &source)
		: source_(source), index_(arena), scratch_(arena) {}
	void append(std::uint64_t ordinal, std::uint64_t revision) {
		if (sorting_)
			throw std::logic_error("sealed mutation index");
		source_.mutation(ordinal);
		index_.push_back({ordinal, revision});
		scratch_.push_back({});
	}
	void seal() { sorting_ = true; }
	bool done() const noexcept { return done_; }
	std::uint64_t size() const {
		if (!done_)
			throw std::logic_error("unsealed mutation index");
		return collapsed_;
	}
	Entry operator[](std::uint64_t ordinal) const {
		if (ordinal >= size())
			throw std::out_of_range("mutation index ordinal");
		return (alternate_ ? scratch_ : index_)[ordinal];
	}
	void step(ValidationBudget &budget) {
		if (!sorting_)
			throw std::logic_error("mutation collection incomplete");
		while (!done_ && budget.rows) {
			if (!charge_)
				charge_ = 256;
			auto n = std::min(charge_, budget.bytes);
			charge_ -= n;
			budget.bytes -= n;
			if (charge_)
				return;
			--budget.rows;
			if (collapsing_)
				collapse_one();
			else
				sort_one();
		}
	}

  private:
	int compare(const Entry &a, const Entry &b) const {
		const auto &x = source_.mutation(a.ordinal), &y = source_.mutation(b.ordinal);
		if (x.namespace_index != y.namespace_index)
			return x.namespace_index < y.namespace_index ? -1 : 1;
		return x.key.compare(y.key);
	}
	void sort_one() {
		const auto n = index_.size();
		if (width_ >= n) {
			collapsing_ = true;
			position_ = collapsed_ = 0;
			return;
		}
		auto &input = alternate_ ? scratch_ : index_;
		auto &output = alternate_ ? index_ : scratch_;
		if (run_ >= n) {
			alternate_ = !alternate_;
			width_ *= 2;
			run_ = 0;
			initialized_ = false;
			return;
		}
		if (!initialized_) {
			left_ = run_;
			middle_ = std::min(run_ + width_, n);
			right_ = middle_;
			end_ = std::min(middle_ + width_, n);
			position_ = run_;
			initialized_ = true;
		}
		if (position_ == end_) {
			run_ = end_;
			initialized_ = false;
			return;
		}
		if (left_ < middle_ && (right_ == end_ || compare(input[left_], input[right_]) <= 0))
			output[position_++] = input[left_++];
		else
			output[position_++] = input[right_++];
	}
	void collapse_one() {
		auto &sorted = alternate_ ? scratch_ : index_;
		if (position_ == sorted.size()) {
			done_ = true;
			return;
		}
		auto value = sorted[position_++];
		if (collapsed_ && compare(sorted[collapsed_ - 1], value) == 0)
			sorted[collapsed_ - 1] = value;
		else
			sorted[collapsed_++] = value;
	}
	const distribution::ValidatedDelta &source_;
	distribution::StableRows<Entry> index_, scratch_;
	std::size_t charge_ = 0;
	std::uint64_t position_ = 0, width_ = 1, run_ = 0, left_ = 0, middle_ = 0, right_ = 0, end_ = 0,
				  collapsed_ = 0;
	bool sorting_ = false, collapsing_ = false, alternate_ = false, initialized_ = false, done_ = false;
};
} // namespace detent::local::detail
