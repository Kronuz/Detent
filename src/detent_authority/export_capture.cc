#include "export_capture.hh"
#include "deferred_values.hh"
#include <algorithm>
namespace detent::authority {
ExportCapture::ExportCapture(FactoryTag, const State &state, std::shared_ptr<const AuthorizedScope> scope)
	: scope_(std::move(scope)), cursor_{state.context_.cluster, state.context_.group, state.context_.epoch,
										scope_->identity(), state.revision_},
	  records_(state.records_), namespaces_(state.namespaces_), definitions_(state.definitions_) {}
Revision ExportCapture::namespace_revision(std::uint64_t ordinal) const {
	if (retired_)
		throw std::logic_error("retired export capture");
	auto name = scope_->name(ordinal);
	const auto *value = namespaces_.find(name);
	return value ? *value : 0;
}
ExportCapture::RecordCursor::RecordCursor(const Records &records,
										  std::shared_ptr<const AuthorizedScope> scope,
										  std::shared_ptr<detail::DeferredValues<KnobDef>> definitions,
										  std::uint32_t index)
	: scope_(std::move(scope)), definitions_(std::move(definitions)),
	  cursor_(start(records, scope_->name(index))), name_(scope_->name(index)), index_(index) {}
ExportCapture::Records::Cursor ExportCapture::RecordCursor::start(const Records &records,
																  std::string_view name) {
	std::array<char, 200> prefix{};
	if (name.size() + 1 > prefix.size())
		throw std::invalid_argument("export namespace bound");
	std::copy(name.begin(), name.end(), prefix.begin());
	prefix[name.size()] = ':';
	return records.cursor_from(std::string_view(prefix.data(), name.size() + 1));
}
std::optional<ExportCapture::RecordCursor::Row> ExportCapture::RecordCursor::next() {
	if (ended_)
		return {};
	if (auto row = cursor_.next()) {
		const auto &key = *row->first;
		if (key.size() > name_.size() && key.compare(0, name_.size(), name_) == 0 && key[name_.size()] == ':')
			return Row{index_, key, row->second->revision, row->second->value.get()};
	}
	ended_ = true;
	return {};
}
ExportCapture::RecordCursor ExportCapture::records(std::uint64_t ordinal) const {
	if (retired_)
		throw std::logic_error("retired export capture");
	scope_->name(ordinal); // Check before narrowing the ordinal.
	return {records_, scope_, definitions_, static_cast<std::uint32_t>(ordinal)};
}

void ExportCapture::retire() noexcept {
	retired_ = true;
	records_.retire_root();
	namespaces_.retire_root();
}
bool ExportCapture::reclamation_pending() const noexcept {
	return records_.reclamation_pending() || namespaces_.reclamation_pending() || definitions_->pending();
}
std::size_t ExportCapture::collect(CommandWorkBudget &budget) noexcept {
	std::size_t freed = 0;
	unsigned idle = 0;
	while (budget.receipts && budget.bytes >= 128 && idle < 3) {
		const auto before = budget.receipts;
		const auto lane = collection_cursor_;
		collection_cursor_ = (collection_cursor_ + 1) % 3;
		std::size_t count = 0;
		if (lane == 0)
			count = records_.collect(1);
		else if (lane == 1)
			count = namespaces_.collect(1);
		else
			count = detail::collect_definition(*definitions_, budget);
		if (count && before == budget.receipts) {
			--budget.receipts;
			budget.bytes -= 128;
		}
		freed += count;
		idle = before == budget.receipts ? idle + 1 : 0;
	}
	return freed;
}
std::shared_ptr<ExportCapture> State::capture(std::shared_ptr<const AuthorizedScope> scope) const {
	if (!scope)
		throw std::invalid_argument("missing authorized export scope");
	auto account = scope->storage_account();
	return std::allocate_shared<ExportCapture>(distribution::StorageAllocator<ExportCapture>(account),
											   ExportCapture::FactoryTag{}, *this, std::move(scope));
}
} // namespace detent::authority
