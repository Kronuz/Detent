#pragma once
#include "allocator.hh"
#include "detent/cursor.hh"
#include "detent_distribution/definition.hh"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace detent::distribution {
enum class UpdateKind : std::uint8_t { Snapshot = 1, Delta = 2, Progress = 3 };
struct Limits {
	std::uint64_t bytes = 256ull * 1024 * 1024, string_bytes = 128ull * 1024 * 1024;
	std::uint32_t records = 500000, namespaces = 4096, overrides_per_definition = 256;
	std::uint32_t definition_bytes = 16 * 1024;
	std::size_t storage_bytes = 256ull * 1024 * 1024;
	std::size_t storage_page_bytes = 65536;
	PagedStorage::Allocate storage_allocate = std::malloc;
	PagedStorage::Release storage_release = std::free;
	std::shared_ptr<StorageLedger> storage_account;
};
struct WorkBudget {
	std::size_t bytes = 8192, tokens = 128;
};
struct NamespaceRevision {
	std::string_view name;
	std::uint64_t revision = 0;
};
struct Record {
	std::uint32_t namespace_index = 0;
	std::string_view key;
	std::uint64_t revision = 0;
	WireDefinition definition;
};
namespace detail {
inline Limits admitted_limits(Limits limits) {
	if (!limits.storage_account)
		limits.storage_account = std::make_shared<StorageLedger>(limits.storage_bytes);
	return limits;
}
struct SnapshotStorage {
	explicit SnapshotStorage(const Limits &limits)
		: account(limits.storage_account ? limits.storage_account
										 : std::make_shared<StorageLedger>(limits.storage_bytes)),
		  arena(*account, limits.storage_page_bytes, limits.storage_allocate, limits.storage_release,
				limits.storage_bytes),
		  namespaces(arena), records(arena), overrides(arena) {}
	std::shared_ptr<StorageLedger> account;
	PagedStorage arena;
	StableRows<NamespaceRevision> namespaces;
	StableRows<Record> records;
	StableRows<OverrideRow> overrides;
	std::size_t retirement_credit = 0;
	bool retiring = false;
};
} // namespace detail
class SnapshotDecoder;
// Only a complete semantic decoder mints this capability. Records contain
// subscriber definitions only, never producer receipts or Raft recovery state.
class ValidatedSnapshot {
  public:
	ValidatedSnapshot(ValidatedSnapshot &&other) noexcept
		: cursor_(std::exchange(other.cursor_, {})), profile_(other.profile_),
		  storage_(std::move(other.storage_)) {}
	ValidatedSnapshot(const ValidatedSnapshot &) = delete;
	ValidatedSnapshot &operator=(ValidatedSnapshot &&) = delete;
	const Cursor &cursor() const noexcept { return cursor_; }
	const Profile &profile() const noexcept { return profile_; }
	const StableRows<NamespaceRevision> &namespaces() const {
		if (!storage_)
			throw std::logic_error("moved distribution snapshot");
		return storage_->namespaces;
	}
	const StableRows<Record> &records() const {
		if (!storage_)
			throw std::logic_error("moved distribution snapshot");
		return storage_->records;
	}
	std::size_t storage_capacity() const noexcept { return storage_ ? storage_->arena.used() : 0; }
	std::size_t peak_storage_capacity() const noexcept { return storage_ ? storage_->arena.peak() : 0; }
	OverrideRange overrides(const WireBoolDef &definition) const {
		if (!storage_ || definition.overrides.start > storage_->overrides.size() ||
			definition.overrides.count > storage_->overrides.size() - definition.overrides.start)
			throw std::out_of_range("distribution override range");
		return {storage_->overrides, definition.overrides.start, definition.overrides.count};
	}
	std::shared_ptr<StorageLedger> storage_account() const {
		if (!storage_)
			throw std::logic_error("moved distribution storage");
		return storage_->account;
	}
	bool discard(WorkBudget &budget);

  private:
	explicit ValidatedSnapshot(const Limits &limits)
		: storage_([&] {
			  const auto bounds = detail::admitted_limits(limits);
			  return make_storage_owner<detail::SnapshotStorage>(
				  bounds.storage_account, bounds.storage_allocate, bounds.storage_release, bounds);
		  }()) {}
	Cursor cursor_;
	Profile profile_;
	StorageOwner<detail::SnapshotStorage> storage_;
	friend class SnapshotDecoder;
};
class SnapshotDecoder {
  public:
	SnapshotDecoder(Cursor expected, std::vector<std::string> namespaces, Limits limits = {});
	~SnapshotDecoder();
	SnapshotDecoder(SnapshotDecoder &&) noexcept;
	SnapshotDecoder(const SnapshotDecoder &) = delete;
	// Advances the input span by exactly the consumed bytes. Zero budget never
	// consumes input. Reaching the root end still requires finish() to certify
	// final framing and absence of trailing input.
	void feed(std::span<const char> &input, WorkBudget &budget);
	ValidatedSnapshot finish();
	bool discard(WorkBudget &budget);

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};

struct Mutation {
	std::uint32_t namespace_index = 0;
	std::string_view key;
	std::optional<WireDefinition> value; // absent means delete live definition.
};
struct Transaction {
	std::uint64_t revision = 0;
	RowRange touched, mutations;
};
namespace detail {
struct DeltaStorage {
	explicit DeltaStorage(const Limits &limits)
		: account(limits.storage_account ? limits.storage_account
										 : std::make_shared<StorageLedger>(limits.storage_bytes)),
		  arena(*account, limits.storage_page_bytes, limits.storage_allocate, limits.storage_release,
				limits.storage_bytes),
		  transactions(arena), mutations(arena), touched(arena), overrides(arena), indexes(arena) {}
	std::shared_ptr<StorageLedger> account;
	PagedStorage arena;
	StableRows<Transaction> transactions;
	StableRows<Mutation> mutations;
	StableRows<NamespaceRevision> touched;
	StableRows<OverrideRow> overrides;
	StableRows<std::uint32_t> indexes;
	std::size_t retirement_credit = 0;
	bool retiring = false;
};
} // namespace detail
class DeltaDecoder;
class ValidatedDelta {
  public:
	ValidatedDelta(ValidatedDelta &&other) noexcept
		: base_(std::exchange(other.base_, {})), target_(std::exchange(other.target_, {})),
		  profile_(other.profile_), kind_(other.kind_), storage_(std::move(other.storage_)) {}
	ValidatedDelta(const ValidatedDelta &) = delete;
	ValidatedDelta &operator=(ValidatedDelta &&) = delete;
	const Cursor &base() const noexcept { return base_; }
	const Cursor &target() const noexcept { return target_; }
	const Profile &profile() const noexcept { return profile_; }
	UpdateKind kind() const noexcept { return kind_; }
	const StableRows<Transaction> &transactions() const {
		if (!storage_)
			throw std::logic_error("moved distribution delta");
		return storage_->transactions;
	}
	std::size_t storage_capacity() const noexcept { return storage_ ? storage_->arena.used() : 0; }
	std::size_t peak_storage_capacity() const noexcept { return storage_ ? storage_->arena.peak() : 0; }
	const Mutation &mutation(std::uint64_t ordinal) const;
	const NamespaceRevision &touched(std::uint64_t ordinal) const;
	OverrideRange overrides(const WireBoolDef &definition) const {
		if (!storage_ || definition.overrides.start > storage_->overrides.size() ||
			definition.overrides.count > storage_->overrides.size() - definition.overrides.start)
			throw std::out_of_range("distribution override range");
		return {storage_->overrides, definition.overrides.start, definition.overrides.count};
	}
	std::shared_ptr<StorageLedger> storage_account() const {
		if (!storage_)
			throw std::logic_error("moved distribution storage");
		return storage_->account;
	}
	bool discard(WorkBudget &budget);

  private:
	explicit ValidatedDelta(const Limits &limits)
		: storage_([&] {
			  const auto bounds = detail::admitted_limits(limits);
			  return make_storage_owner<detail::DeltaStorage>(bounds.storage_account, bounds.storage_allocate,
															  bounds.storage_release, bounds);
		  }()) {}
	Cursor base_, target_;
	Profile profile_;
	UpdateKind kind_ = UpdateKind::Delta;
	StorageOwner<detail::DeltaStorage> storage_;
	friend class DeltaDecoder;
};
class DeltaDecoder {
  public:
	DeltaDecoder(Cursor expected_base, std::vector<std::string> namespaces, Limits limits = {},
				 UpdateKind kind = UpdateKind::Delta);
	~DeltaDecoder();
	DeltaDecoder(DeltaDecoder &&) noexcept;
	DeltaDecoder(const DeltaDecoder &) = delete;
	void feed(std::span<const char> &input, WorkBudget &budget);
	ValidatedDelta finish();
	bool discard(WorkBudget &budget);

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
