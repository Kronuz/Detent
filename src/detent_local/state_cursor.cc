#include "state_cursor.hh"
#include "detent_local/paged_definition.hh"
namespace detent::local::detail {
StateCursor::StateCursor(RecoveredGeneration generation, std::shared_ptr<distribution::StorageLedger> account)
	: generation_(std::move(generation)) {
	if (!account)
		throw std::invalid_argument("state cursor requires a storage account");
	if (generation_.paged_) {
		namespaces_.emplace(account, generation_, paged::PageKind::Namespace);
		records_.emplace(account, generation_, paged::PageKind::Definition);
	} else {
		flat_.emplace(generation_);
	}
}
bool StateCursor::pay(ValidationBudget &budget, std::size_t bytes) {
	if (!budget.rows)
		return false;
	if (!charge_)
		charge_ = bytes;
	const auto amount = std::min(charge_, budget.bytes);
	charge_ -= amount;
	budget.bytes -= amount;
	if (charge_)
		return false;
	--budget.rows;
	return true;
}
bool StateCursor::namespaces_done() const noexcept {
	return flat_ ? namespace_ordinal_ == flat_->namespace_count() : namespaces_->done();
}
bool StateCursor::records_done() const noexcept {
	return flat_ ? record_ordinal_ == flat_->record_count() : records_->done();
}
bool StateCursor::next_namespace(ValidationBudget &budget) {
	if (record_ready_ || remaining_overrides_)
		throw std::logic_error("state namespace stream must precede records");
	if (namespace_ready_ && namespaces_)
		namespaces_->consume();
	namespace_ready_ = false;
	if (flat_) {
		if (namespaces_done())
			return false;
		const auto row = flat_->name_space(namespace_ordinal_);
		if (!pay(budget, row.name.size() + 8))
			return false;
		namespace_row_ = row;
		++namespace_ordinal_;
	} else {
		if (!namespaces_->ready())
			namespaces_->step(budget);
		if (!namespaces_->ready())
			return false;
		const auto leaf = namespaces_->leaf();
		if (!pay(budget, leaf.key.size() + 8))
			return false;
		const auto key = paged::decode_key(leaf.key);
		namespace_row_ = {key.name_space, read_integer(leaf.body, 0, 8)};
	}
	namespace_ready_ = true;
	return true;
}
bool StateCursor::next_record(ValidationBudget &budget) {
	if (!namespaces_done() || remaining_overrides_)
		throw std::logic_error("state record stream sequencing");
	if (record_ready_ && records_)
		records_->consume();
	record_ready_ = false;
	override_ready_ = false;
	if (flat_) {
		if (records_done())
			return false;
		const auto row = flat_->record(record_ordinal_);
		if (!pay(budget, row.key.size() + 32))
			return false;
		const auto name = flat_->name_space(row.namespace_index).name;
		record_row_ = {name, row.key.substr(name.size() + 1), row.revision, row.definition};
		if (const auto *flag = std::get_if<SourceBoolean>(&row.definition)) {
			flat_override_ = flag->override_start;
			remaining_overrides_ = flag->override_count;
		}
		++record_ordinal_;
	} else {
		if (!records_->ready())
			records_->step(budget);
		if (!records_->ready())
			return false;
		const auto leaf = records_->leaf();
		if (!pay(budget, leaf.key.size() + paged::definition_header_bytes))
			return false;
		const auto key = paged::decode_key(leaf.key);
		const auto definition = paged::decode_definition_header(leaf.body, UINT64_MAX);
		record_row_ = {key.name_space, key.local_name, definition.revision, IntDef{definition.default_int}};
		if (definition.boolean) {
			record_row_.definition =
				SourceBoolean{definition.default_bool, definition.rollout, 0, definition.overrides};
			override_bytes_ = leaf.body;
			override_offset_ = paged::definition_header_bytes;
			remaining_overrides_ = definition.overrides;
		}
	}
	record_ready_ = true;
	return true;
}
bool StateCursor::next_override(ValidationBudget &budget) {
	override_ready_ = false;
	if (!remaining_overrides_)
		return false;
	SourceOverride row;
	std::size_t next_offset = override_offset_;
	if (flat_) {
		row = flat_->override_value(flat_override_);
	} else {
		const auto value = paged::read_override(override_bytes_, next_offset);
		row = {value.key, value.percentage ? GroupOverride{GroupRollout{value.value}}
										   : GroupOverride{bool(value.value)}};
	}
	if (!pay(budget, row.name.size() + 4))
		return false;
	override_row_ = row;
	override_offset_ = next_offset;
	++flat_override_;
	--remaining_overrides_;
	override_ready_ = true;
	return true;
}
SourceNamespace StateCursor::name_space() const {
	if (!namespace_ready_)
		throw std::logic_error("state namespace not ready");
	return namespace_row_;
}
StateRecord StateCursor::record() const {
	if (!record_ready_)
		throw std::logic_error("state record not ready");
	return record_row_;
}
SourceOverride StateCursor::override_value() const {
	if (!override_ready_)
		throw std::logic_error("state override not ready");
	return override_row_;
}
std::optional<paged::Digest> StateCursor::logical_commitment(const RecoveredGeneration &generation) {
	return generation.paged_ ? std::optional(generation.paged_->root().logical) : std::nullopt;
}
void StateCursor::reset() {
	if (namespaces_) {
		namespaces_->rewind();
		records_->rewind();
	}
	namespace_ordinal_ = record_ordinal_ = 0;
	flat_override_ = remaining_overrides_ = charge_ = 0;
	override_bytes_ = {};
	namespace_ready_ = record_ready_ = override_ready_ = false;
}
} // namespace detent::local::detail
