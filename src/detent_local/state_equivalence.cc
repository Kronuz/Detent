#include "state_equivalence.hh"
namespace detent::local::detail {
StateEquivalence::StateEquivalence(RecoveredGeneration left, RecoveredGeneration right,
								   std::shared_ptr<distribution::StorageLedger> account) {
	if (!account)
		throw std::invalid_argument("state equivalence requires a storage account");
	if (left.descriptor().cursor != right.descriptor().cursor ||
		left.descriptor().profile != right.descriptor().profile) {
		mismatch();
	} else if (const auto a = StateCursor::logical_commitment(left),
			   b = StateCursor::logical_commitment(right);
			   a && b) {
		done_ = true;
		equal_ = *a == *b;
	} else {
		left_.emplace(std::move(left), account);
		right_.emplace(std::move(right), std::move(account));
	}
}
bool StateEquivalence::texts(std::span<const std::pair<std::string_view, std::string_view>> pairs,
							 ValidationBudget &budget) {
	while (text_index_ < pairs.size()) {
		const auto [a, b] = pairs[text_index_];
		if (a.size() != b.size()) {
			mismatch();
			return true;
		}
		const auto length = std::min(a.size() - text_offset_, budget.bytes);
		if (a.substr(text_offset_, length) != b.substr(text_offset_, length)) {
			mismatch();
			return true;
		}
		text_offset_ += length;
		budget.bytes -= length;
		if (text_offset_ != a.size())
			return false;
		text_offset_ = 0;
		++text_index_;
	}
	return true;
}
void StateEquivalence::step(ValidationBudget &budget) {
	while (!done_ && budget.rows) {
		auto advance = [&](StateCursor &cursor, bool &ready) {
			if (ready)
				return;
			switch (phase_) {
			case Phase::Namespaces:
				ready = cursor.next_namespace(budget);
				break;
			case Phase::Records:
				ready = cursor.next_record(budget);
				break;
			case Phase::Overrides:
				ready = cursor.next_override(budget);
				break;
			}
		};
		advance(*left_, left_ready_);
		advance(*right_, right_ready_);
		auto exhausted = [&](const StateCursor &cursor, bool ready) {
			if (ready)
				return false;
			switch (phase_) {
			case Phase::Namespaces:
				return cursor.namespaces_done();
			case Phase::Records:
				return cursor.records_done();
			case Phase::Overrides:
				return cursor.overrides_done();
			}
			return false;
		};
		const auto left_end = exhausted(*left_, left_ready_), right_end = exhausted(*right_, right_ready_);
		if ((left_end && right_ready_) || (right_end && left_ready_)) {
			mismatch();
			return;
		}
		if (left_end && right_end) {
			if (phase_ == Phase::Namespaces)
				phase_ = Phase::Records;
			else if (phase_ == Phase::Overrides)
				phase_ = Phase::Records;
			else
				done_ = true;
			continue;
		}
		if (!left_ready_ || !right_ready_)
			return;
		bool same = true;
		std::array<std::pair<std::string_view, std::string_view>, 2> pairs{};
		std::size_t pair_count = 1;
		if (phase_ == Phase::Namespaces) {
			const auto a = left_->name_space(), b = right_->name_space();
			same = a.revision == b.revision;
			pairs[0] = {a.name, b.name};
		} else if (phase_ == Phase::Overrides) {
			const auto a = left_->override_value(), b = right_->override_value();
			same = a.value == b.value;
			pairs[0] = {a.name, b.name};
		} else {
			const auto a = left_->record(), b = right_->record();
			pairs[0] = {a.name_space, b.name_space};
			pairs[1] = {a.key, b.key};
			pair_count = 2;
			same = a.revision == b.revision && a.definition.index() == b.definition.index();
			if (same) {
				if (const auto *flag = std::get_if<SourceBoolean>(&a.definition)) {
					const auto &other = std::get<SourceBoolean>(b.definition);
					same = flag->default_value == other.default_value &&
						   flag->rollout_pct == other.rollout_pct &&
						   flag->override_count == other.override_count;
				} else {
					same = std::get<IntDef>(a.definition) == std::get<IntDef>(b.definition);
				}
			}
		}
		if (!same) {
			mismatch();
			return;
		}
		if (!texts({pairs.data(), pair_count}, budget) || done_)
			return;
		text_index_ = text_offset_ = 0;
		if (phase_ == Phase::Records)
			phase_ = Phase::Overrides;
		left_ready_ = right_ready_ = false;
	}
}
bool StateEquivalence::equal() const {
	if (!done_)
		throw std::logic_error("state equivalence is incomplete");
	return equal_;
}
} // namespace detent::local::detail
