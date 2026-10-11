#include "paged_snapshot_source.hh"
#include "detent_local/paged_definition.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
PagedSnapshotSource::PagedSnapshotSource(RecoveredGeneration generation,
										 std::shared_ptr<d::StorageLedger> account)
	: descriptor_(generation.descriptor()), account_(std::move(account)),
	  namespaces_cursor_(account_, generation, p::PageKind::Namespace),
	  records_cursor_(account_, generation, p::PageKind::Definition),
	  namespaces_(d::StorageAllocator<Namespace>(account_)) {
	if (!account_ || descriptor_.profile != d::Profile{} || !generation.paged_)
		throw std::invalid_argument("unsupported paged snapshot source");
	expected_namespaces_ = generation.paged_->root().namespaces;
	namespaces_.reserve(expected_namespaces_);
}
bool PagedSnapshotSource::prepare(d::WorkBudget &budget) {
	if (prepared_)
		return true;
	while (budget.tokens) {
		if (!namespaces_cursor_.ready() && !namespaces_cursor_.done()) {
			ValidationBudget work{budget.bytes, budget.tokens};
			namespaces_cursor_.step(work);
			budget.bytes = work.bytes;
			budget.tokens = work.rows;
		}
		if (namespaces_cursor_.done()) {
			if (namespaces_.size() != expected_namespaces_)
				throw std::logic_error("paged export namespace count");
			prepared_ = true;
			return true;
		}
		if (!namespaces_cursor_.ready() || !budget.tokens)
			return false;
		auto n = std::min(preparation_charge_, budget.bytes);
		preparation_charge_ -= n;
		budget.bytes -= n;
		if (preparation_charge_)
			return false;
		--budget.tokens;
		preparation_charge_ = 256;
		const auto leaf = namespaces_cursor_.leaf();
		const auto key = p::decode_key(leaf.key);
		if (key.name_space.size() > 198 || leaf.body.size() != 8 ||
			namespaces_.size() == expected_namespaces_)
			throw std::logic_error("paged export namespace framing");
		Namespace row;
		row.length = key.name_space.size();
		std::copy(key.name_space.begin(), key.name_space.end(), row.text.begin());
		row.revision = read_integer(leaf.body, 0, 8);
		if (!namespaces_.empty() && namespaces_.back().name() >= row.name())
			throw std::logic_error("paged export namespace order");
		namespaces_.push_back(row);
		namespaces_cursor_.consume();
	}
	return false;
}
d::NamespaceRevision PagedSnapshotSource::name_space(std::uint64_t ordinal) const {
	if (!prepared_ || ordinal >= namespaces_.size())
		throw std::out_of_range("paged export namespace ordinal");
	const auto &row = namespaces_[ordinal];
	return {row.name(), row.revision};
}
void PagedSnapshotSource::reset_records() noexcept {
	records_cursor_.rewind();
	overrides_ = {};
	override_offset_ = 0;
	overrides_remaining_ = 0;
}
d::SnapshotRecordStep PagedSnapshotSource::next_record() {
	if (!prepared_ || overrides_remaining_)
		throw std::logic_error("paged export record sequencing");
	ValidationBudget work{512, 1};
	records_cursor_.step(work);
	if (records_cursor_.done())
		return {{}, true};
	if (!records_cursor_.ready())
		return {{}, false};
	const auto leaf = records_cursor_.leaf();
	const auto key = p::decode_key(leaf.key);
	const auto ns =
		std::lower_bound(namespaces_.begin(), namespaces_.end(), key.name_space,
						 [](const auto &row, std::string_view name) { return row.name() < name; });
	if (ns == namespaces_.end() || ns->name() != key.name_space)
		throw std::logic_error("paged export namespace binding");
	const auto definition = p::decode_definition_header(leaf.body, ns->revision);
	const auto length = key.name_space.size() + key.local_name.size() + 1;
	if (length > record_key_.size())
		throw std::logic_error("paged export record key length");
	std::copy(key.name_space.begin(), key.name_space.end(), record_key_.begin());
	record_key_[key.name_space.size()] = ':';
	std::copy(key.local_name.begin(), key.local_name.end(), record_key_.begin() + key.name_space.size() + 1);
	d::WireDefinition value;
	if (definition.boolean) {
		value = d::WireBoolDef{definition.default_bool, {0, definition.overrides}, definition.rollout};
		overrides_ = leaf.body;
		override_offset_ = p::definition_header_bytes;
		overrides_remaining_ = definition.overrides;
	} else {
		value = IntDef{definition.default_int};
		overrides_ = {};
	}
	records_cursor_.consume();
	return {d::Record{static_cast<std::uint32_t>(ns - namespaces_.begin()),
					  {record_key_.data(), length},
					  definition.revision,
					  value},
			false};
}
std::optional<d::OverrideRow> PagedSnapshotSource::next_override() {
	if (!overrides_remaining_)
		return {};
	const auto row = p::read_override(overrides_, override_offset_);
	--overrides_remaining_;
	GroupOverride value =
		row.percentage ? GroupOverride{GroupRollout{row.value}} : GroupOverride{bool(row.value)};
	return d::OverrideRow{row.key, value};
}
} // namespace detent::local::detail
