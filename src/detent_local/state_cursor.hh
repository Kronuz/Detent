#pragma once
#include "paged_cursor.hh"
#include "pinned_source.hh"
#include "validated_flat_state.hh"
namespace detent::local::detail {
struct StateRecord {
	std::string_view name_space, key;
	std::uint64_t revision = 0;
	std::variant<SourceBoolean, IntDef> definition;
};
// Borrowed rows remain valid until the next advance of the same stream.
class StateCursor {
  public:
	StateCursor(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
	StateCursor(ValidatedFlatState, std::shared_ptr<distribution::StorageLedger>);
	std::uint32_t namespace_count() const;
	std::uint32_t record_count() const;
	bool next_namespace(ValidationBudget &);
	bool next_record(ValidationBudget &);
	bool next_override(ValidationBudget &);
	bool namespaces_done() const noexcept;
	bool records_done() const noexcept;
	bool overrides_done() const noexcept { return remaining_overrides_ == 0; }
	SourceNamespace name_space() const;
	StateRecord record() const;
	SourceOverride override_value() const;
	void reset();
	const Descriptor &descriptor() const noexcept { return descriptor_; }
	static std::optional<paged::Digest> logical_commitment(const RecoveredGeneration &);
	std::optional<DirectoryIdentity> directory_identity() const;

  private:
	std::optional<RecoveredGeneration> generation_;
	Descriptor descriptor_;
	std::optional<FlatStateRows> flat_;
	std::optional<PagedCursor> namespaces_, records_;
	std::uint32_t namespace_ordinal_ = 0, record_ordinal_ = 0;
	std::uint64_t flat_override_ = 0, remaining_overrides_ = 0;
	std::span<const char> override_bytes_;
	std::size_t override_offset_ = 0, charge_ = 0;
	SourceNamespace namespace_row_;
	StateRecord record_row_;
	SourceOverride override_row_;
	bool namespace_ready_ = false, record_ready_ = false, override_ready_ = false;
	bool pay(ValidationBudget &, std::size_t);
};
} // namespace detent::local::detail
