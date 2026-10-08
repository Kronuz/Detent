#pragma once
#include "detent_distribution/protocol.hh"
#include "pinned_source.hh"

namespace detent::local::detail {
// Worker-owned borrowed merge. Text and definitions remain in the pinned image
// or validated delta; only the ordered row references consume scratch pages.
class AppliedImageSource {
	struct Index {
		std::uint64_t ordinal = 0, revision = 0;
	};
	struct RecordRef {
		std::uint64_t ordinal = 0, revision = 0;
		bool changed = false;
	};

  public:
	AppliedImageSource(RecoveredGeneration base, std::shared_ptr<const distribution::ValidatedDelta> delta,
					   ViewLimits limits)
		: base_(std::move(base)), delta_(std::move(delta)), arena_(*account(delta_)), namespaces_(arena_),
		  index_(arena_), scratch_(arena_), records_(arena_), limits_(limits) {
		if (base_.descriptor().cursor != delta_->base() || base_.descriptor().profile != delta_->profile())
			throw std::invalid_argument("distribution update does not match pinned base");
		if (base_.namespace_count() > limits_.namespaces)
			throw std::length_error("pinned scope exceeds image namespace limit");
	}
	void step(ValidationBudget &budget) {
		while (phase_ < 5 && budget.rows) {
			if (!charge_)
				charge_ = 256;
			auto bytes = std::min<std::size_t>(charge_, budget.bytes);
			charge_ -= bytes;
			budget.bytes -= bytes;
			if (charge_)
				return;
			--budget.rows;
			if (phase_ == 0) {
				if (position_ < base_.namespace_count())
					namespaces_.push_back(base_.name_space(position_++));
				else {
					position_ = 0;
					phase_ = 1;
				}
			} else if (phase_ == 1)
				collect();
			else if (phase_ == 2)
				sort_one();
			else if (phase_ == 3)
				collapse_one();
			else
				merge_one();
		}
	}
	bool done() const noexcept { return phase_ == 5; }
	std::uint64_t namespace_count() const noexcept { return namespaces_.size(); }
	std::uint64_t record_count() const noexcept { return records_.size(); }
	SourceNamespace name_space(std::uint64_t ordinal) const { return namespaces_[ordinal]; }
	SourceRecord record(std::uint64_t ordinal) const {
		const auto ref = records_[ordinal];
		if (!ref.changed)
			return base_.record(ref.ordinal);
		const auto &m = delta_->mutation(ref.ordinal);
		SourceRecord result{m.namespace_index, m.key, ref.revision, IntDef{}};
		if (const auto *b = std::get_if<distribution::WireBoolDef>(&*m.value))
			result.definition =
				SourceBoolean{b->default_value, b->rollout_pct, b->overrides.start, b->overrides.count};
		else
			result.definition = std::get<IntDef>(*m.value);
		return result;
	}
	SourceOverride override_value(std::uint64_t ordinal, std::uint64_t relative) const {
		const auto ref = records_[ordinal];
		if (!ref.changed) {
			const auto b = std::get<SourceBoolean>(base_.record(ref.ordinal).definition);
			if (relative >= b.override_count)
				throw std::out_of_range("source override");
			return base_.override_value(b.override_start + relative);
		}
		const auto &b = std::get<distribution::WireBoolDef>(*delta_->mutation(ref.ordinal).value);
		const auto &r = delta_->overrides(b)[relative];
		return {r.first, r.second};
	}
	const Descriptor &base_descriptor() const noexcept { return base_.descriptor(); }
	DirectoryIdentity directory_identity() const noexcept { return base_.directory_identity(); }

	bool discard(ValidationBudget &budget) {
		while (arena_.front_capacity() && budget.rows) {
			if (!disposal_charge_)
				disposal_charge_ = arena_.front_capacity();
			auto n = std::min(disposal_charge_, budget.bytes);
			disposal_charge_ -= n;
			budget.bytes -= n;
			if (disposal_charge_)
				return false;
			--budget.rows;
			arena_.release_one();
		}
		return !arena_.front_capacity();
	}

  private:
	static std::shared_ptr<distribution::StorageLedger>
	account(const std::shared_ptr<const distribution::ValidatedDelta> &delta) {
		if (!delta)
			throw std::invalid_argument("missing validated delta");
		return delta->storage_account();
	}
	std::size_t charge_ = 0, disposal_charge_ = 0;
	int compare(const Index &a, const Index &b) const {
		const auto &x = delta_->mutation(a.ordinal), &y = delta_->mutation(b.ordinal);
		if (x.namespace_index != y.namespace_index)
			return x.namespace_index < y.namespace_index ? -1 : 1;
		return x.key.compare(y.key);
	}
	void collect() {
		if (transaction_ == delta_->transactions().size()) {
			phase_ = 2;
			position_ = 0;
			return;
		}
		const auto &tx = delta_->transactions()[transaction_];
		if (touched_ < tx.touched.count) {
			const auto &n = delta_->touched(tx.touched.start + touched_++);
			// Decoder preserves canonical scope order; find by binary search without
			// trusting a namespace index supplied by another scope.
			std::uint64_t low = 0, high = namespaces_.size();
			while (low < high) {
				auto mid = low + (high - low) / 2;
				if (namespaces_[mid].name < n.name)
					low = mid + 1;
				else
					high = mid;
			}
			if (low == namespaces_.size() || namespaces_[low].name != n.name)
				throw std::invalid_argument("delta namespace outside pinned scope");
			namespaces_[low].revision = n.revision;
		} else if (position_ < tx.mutations.count) {
			const auto ordinal = tx.mutations.start + position_++;
			const auto &m = delta_->mutation(ordinal);
			if (m.namespace_index >= namespaces_.size())
				throw std::invalid_argument("delta namespace index");
			const auto name = namespaces_[m.namespace_index].name;
			if (m.key.size() <= name.size() || m.key.substr(0, name.size()) != name ||
				m.key[name.size()] != ':')
				throw std::invalid_argument("delta key outside pinned scope");
			index_.push_back({ordinal, tx.revision});
			scratch_.push_back({});
		} else {
			++transaction_;
			position_ = touched_ = 0;
		}
	}
	void sort_one() {
		const auto n = index_.size();
		if (width_ >= n) {
			phase_ = 3;
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
		// Stable passes preserve transaction order for repeated keys.
		if (left_ < middle_ && (right_ == end_ || compare(input[left_], input[right_]) <= 0))
			output[position_++] = input[left_++];
		else
			output[position_++] = input[right_++];
	}
	void collapse_one() {
		auto &sorted = alternate_ ? scratch_ : index_;
		if (position_ == sorted.size()) {
			phase_ = 4;
			position_ = base_position_ = 0;
			return;
		}
		auto value = sorted[position_++];
		if (collapsed_ && compare(sorted[collapsed_ - 1], value) == 0)
			sorted[collapsed_ - 1] = value;
		else
			sorted[collapsed_++] = value;
	}
	void merge_one() {
		auto &sorted = alternate_ ? scratch_ : index_;
		if (base_position_ == base_.record_count() && position_ == collapsed_) {
			phase_ = 5;
			return;
		}
		int order = 1;
		if (base_position_ < base_.record_count()) {
			order = -1;
			if (position_ < collapsed_) {
				const auto b = base_.record(base_position_);
				const auto &m = delta_->mutation(sorted[position_].ordinal);
				order = b.namespace_index == m.namespace_index
							? b.key.compare(m.key)
							: (b.namespace_index < m.namespace_index ? -1 : 1);
			}
		}
		if (order < 0) {
			admit_record();
			records_.push_back({base_position_++, 0, false});
		} else {
			const auto m = sorted[position_++];
			if (order == 0)
				++base_position_;
			if (delta_->mutation(m.ordinal).value) {
				admit_record();
				records_.push_back({m.ordinal, m.revision, true});
			}
		}
	}
	void admit_record() const {
		if (records_.size() >= limits_.records)
			throw std::length_error("merged image record limit exceeded");
	}
	PinnedImageSource base_;
	std::shared_ptr<const distribution::ValidatedDelta> delta_;
	distribution::PagedStorage arena_;
	distribution::StableRows<SourceNamespace> namespaces_;
	distribution::StableRows<Index> index_, scratch_;
	distribution::StableRows<RecordRef> records_;
	ViewLimits limits_;
	unsigned phase_ = 0;
	std::uint64_t position_ = 0, transaction_ = 0, touched_ = 0, width_ = 1, run_ = 0, left_ = 0, middle_ = 0,
				  right_ = 0, end_ = 0, collapsed_ = 0, base_position_ = 0;
	bool alternate_ = false, initialized_ = false;
};
} // namespace detent::local::detail
