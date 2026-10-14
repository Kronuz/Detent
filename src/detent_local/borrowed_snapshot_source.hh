#pragma once
#include "detent_local/paged_definition.hh"
#include "state_cursor.hh"
namespace detent::local::detail {
// Two borrowed rows supply postorder encoder lookahead without an ordinal table.
class BorrowedSnapshotSource {
  public:
	BorrowedSnapshotSource(RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
	bool prepare(std::uint64_t, ValidationBudget &) const;
	void reset();
	std::size_t key(std::uint64_t, std::span<char>) const;
	paged::Definition definition(std::uint64_t) const;
	paged::Override override_value(std::uint64_t, std::uint32_t) const;
	std::uint64_t watermark(std::uint64_t) const;
	std::uint32_t records() const { return records_.record_count(); }
	std::uint32_t namespaces() const { return namespaces_.namespace_count(); }
	const Descriptor &descriptor() const { return records_.descriptor(); }
	std::shared_ptr<distribution::StorageLedger> storage_account() const { return account_; }

  private:
	struct Slot {
		std::uint64_t ordinal = UINT64_MAX, watermark = 0;
		std::array<char, 202> key{};
		std::size_t length = 0;
		paged::Definition definition;
		std::array<SourceOverride, 256> overrides{};
		std::uint32_t override_count = 0;
		bool ready = false;
	};
	std::shared_ptr<distribution::StorageLedger> account_;
	mutable StateCursor records_, namespaces_;
	mutable std::array<Slot, 2> slots_;
	mutable std::uint64_t next_ = 0;
	mutable std::size_t current_ = 0, charge_ = 0;
	enum class Phase { Acquire, Copy, Overrides };
	mutable Phase phase_ = Phase::Acquire;
	const Slot &slot(std::uint64_t) const;
};
} // namespace detent::local::detail
