#include "state_cursor.hh"
#include "detent_local/paged_definition.hh"
namespace detent::local::detail {
StateCursor::StateCursor(RecoveredGeneration generation, std::shared_ptr<distribution::StorageLedger> account)
	: generation_(std::move(generation)) {
	if (!account)
		throw std::invalid_argument("state cursor requires a storage account");
	descriptor_ = generation_->descriptor();
	namespaces_.emplace(account, *generation_, paged::PageKind::Namespace);
	records_.emplace(account, *generation_, paged::PageKind::Definition);
}
StateCursor::StateCursor(ValidatedPagedState state, std::shared_ptr<distribution::StorageLedger> account)
	: descriptor_(state.descriptor()), candidate_root_(state.root()) {
	if (!account)
		throw std::invalid_argument("candidate paged cursor requires a storage account");
	namespaces_.emplace(account, state.view(), paged::PageKind::Namespace);
	records_.emplace(account, state.view(), paged::PageKind::Definition);
}
std::optional<DirectoryIdentity> StateCursor::directory_identity() const {
	return generation_ ? std::optional(generation_->directory_identity()) : std::nullopt;
}
std::uint32_t StateCursor::namespace_count() const {
	return candidate_root_ ? candidate_root_->namespaces : generation_->paged_->root().namespaces;
}
std::uint32_t StateCursor::record_count() const {
	return candidate_root_ ? candidate_root_->records : generation_->paged_->root().records;
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
	return namespaces_->done();
}
bool StateCursor::records_done() const noexcept {
	return records_->done();
}
bool StateCursor::next_namespace(ValidationBudget &budget) {
	if (record_ready_ || remaining_overrides_)
		throw std::logic_error("state namespace stream must precede records");
	if (namespace_ready_)
		namespaces_->consume();
	namespace_ready_ = false;
	if (!namespaces_->ready())
		namespaces_->step(budget);
	if (!namespaces_->ready())
		return false;
	const auto leaf = namespaces_->leaf();
	if (!pay(budget, leaf.key.size() + 8))
		return false;
	const auto key = paged::decode_key(leaf.key);
	namespace_row_ = {key.name_space, read_integer(leaf.body, 0, 8)};
	namespace_ready_ = true;
	return true;
}
bool StateCursor::next_record(ValidationBudget &budget) {
	if (!namespaces_done() || remaining_overrides_)
		throw std::logic_error("state record stream sequencing");
	if (record_ready_)
		records_->consume();
	record_ready_ = false;
	override_ready_ = false;
	if (!records_->ready())
		records_->step(budget);
	if (!records_->ready())
		return false;
	const auto leaf = records_->leaf();
	if (!pay(budget, leaf.key.size() + paged::definition_header_bytes))
		return false;
	const auto key = paged::decode_key(leaf.key);
	const auto definition = paged::decode_definition_header(leaf.body, UINT64_MAX);
	record_row_ = {key.name_space,
				   key.local_name,
				   definition.revision,
				   {definition.default_value, definition.rollout, definition.rollout_value, 0, definition.overrides}};
	override_bytes_ = leaf.body;
	override_offset_ = paged::header_size(definition);
	remaining_overrides_ = definition.overrides;
	record_ready_ = true;
	return true;
}
bool StateCursor::next_override(ValidationBudget &budget) {
	override_ready_ = false;
	if (!remaining_overrides_)
		return false;
	std::size_t next_offset = override_offset_;
	const auto value = paged::read_override(override_bytes_, next_offset);
	const SourceOverride row{value.key, value.value};
	if (!pay(budget, row.name.size() + 4))
		return false;
	override_row_ = row;
	override_offset_ = next_offset;
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
paged::Digest StateCursor::state_commitment(const RecoveredGeneration &generation) {
	return paged::state_commitment(generation.paged_->root());
}
paged::Digest StateCursor::state_commitment() const {
	return candidate_root_ ? paged::state_commitment(*candidate_root_) : state_commitment(*generation_);
}
void StateCursor::reset() {
	namespaces_->rewind();
	records_->rewind();
	remaining_overrides_ = charge_ = 0;
	override_bytes_ = {};
	namespace_ready_ = record_ready_ = override_ready_ = false;
}
} // namespace detent::local::detail
