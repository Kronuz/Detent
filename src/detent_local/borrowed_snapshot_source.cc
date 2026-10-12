#include "borrowed_snapshot_source.hh"
namespace detent::local::detail {
BorrowedSnapshotSource::BorrowedSnapshotSource(RecoveredGeneration generation,
											   std::shared_ptr<distribution::StorageLedger> account)
	: account_(std::move(account)), records_(generation, account_),
	  namespaces_(std::move(generation), account_) {}
const BorrowedSnapshotSource::Slot &BorrowedSnapshotSource::slot(std::uint64_t ordinal) const {
	for (const auto &row : slots_)
		if (row.ready && row.ordinal == ordinal)
			return row;
	throw std::logic_error("borrowed snapshot row is not ready");
}
bool BorrowedSnapshotSource::prepare(std::uint64_t ordinal, ValidationBudget &budget) const {
	for (const auto &row : slots_)
		if (row.ready && row.ordinal == ordinal)
			return true;
	if (ordinal != next_ || ordinal >= std::uint64_t(records()) + namespaces())
		throw std::logic_error("borrowed snapshot source is sequential");
	auto &row = slots_[current_];
	while (budget.rows) {
		if (phase_ == Phase::Acquire) {
			row.ready = false;
			row.override_count = 0;
			row.definition = {};
			if (ordinal < records()) {
				while (!records_.namespaces_done()) {
					if (!records_.next_namespace(budget) && !records_.namespaces_done())
						return false;
				}
				if (!records_.next_record(budget))
					return false;
			} else if (!namespaces_.next_namespace(budget)) {
				return false;
			}
			phase_ = Phase::Copy;
			charge_ = row.key.size() + paged::definition_header_bytes;
		}
		if (phase_ == Phase::Copy) {
			const auto amount = std::min(charge_, budget.bytes);
			charge_ -= amount;
			budget.bytes -= amount;
			if (charge_ || !budget.rows)
				return false;
			--budget.rows;
			if (ordinal < records()) {
				const auto value = records_.record();
				std::array<char, 200> qualified{};
				const auto length = value.name_space.size() + 1 + value.key.size();
				if (length > qualified.size())
					throw std::logic_error("borrowed snapshot key length");
				std::copy(value.name_space.begin(), value.name_space.end(), qualified.begin());
				qualified[value.name_space.size()] = ':';
				std::copy(value.key.begin(), value.key.end(),
						  qualified.begin() + value.name_space.size() + 1);
				row.length = paged::encode_definition_key(row.key, {qualified.data(), length});
				row.definition.revision = value.revision;
				if (const auto *flag = std::get_if<SourceBoolean>(&value.definition)) {
					if (flag->override_count > row.overrides.size())
						throw std::logic_error("borrowed snapshot override bound");
					row.definition.boolean = true;
					row.definition.default_bool = flag->default_value;
					row.definition.rollout = flag->rollout_pct;
					row.definition.overrides = flag->override_count;
				} else {
					row.definition.default_int = std::get<IntDef>(value.definition).default_value;
				}
			} else {
				const auto value = namespaces_.name_space();
				row.length = paged::encode_namespace_key(row.key, value.name);
				row.watermark = value.revision;
			}
			phase_ = Phase::Overrides;
		}
		if (phase_ == Phase::Overrides) {
			if (ordinal < records()) {
				while (!records_.overrides_done()) {
					if (!records_.next_override(budget))
						return false;
					row.overrides.at(row.override_count++) = records_.override_value();
				}
			}
			if (row.override_count != row.definition.overrides)
				throw std::logic_error("borrowed snapshot override count");
			row.ordinal = ordinal;
			row.ready = true;
			++next_;
			current_ ^= 1;
			phase_ = Phase::Acquire;
			return true;
		}
	}
	return false;
}
std::size_t BorrowedSnapshotSource::key(std::uint64_t ordinal, std::span<char> output) const {
	const auto &row = slot(ordinal);
	if (row.length > output.size())
		throw std::invalid_argument("borrowed snapshot key destination");
	std::copy_n(row.key.begin(), row.length, output.begin());
	return row.length;
}
paged::Definition BorrowedSnapshotSource::definition(std::uint64_t ordinal) const {
	if (ordinal >= records())
		throw std::logic_error("borrowed snapshot definition ordinal");
	return slot(ordinal).definition;
}
paged::Override BorrowedSnapshotSource::override_value(std::uint64_t ordinal, std::uint32_t index) const {
	const auto &row = slot(ordinal);
	if (ordinal >= records() || index >= row.override_count)
		throw std::out_of_range("borrowed snapshot override ordinal");
	const auto &value = row.overrides[index];
	const auto *rollout = std::get_if<GroupRollout>(&value.value);
	return {value.name, rollout != nullptr,
			rollout ? rollout->rollout_pct : static_cast<std::uint8_t>(std::get<bool>(value.value))};
}
std::uint64_t BorrowedSnapshotSource::watermark(std::uint64_t ordinal) const {
	if (ordinal < records())
		throw std::logic_error("borrowed snapshot namespace ordinal");
	return slot(ordinal).watermark;
}
} // namespace detent::local::detail
