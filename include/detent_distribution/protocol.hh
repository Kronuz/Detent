#pragma once
#include "detent/model.hh"
#include <array>
#include <deque>
#include <memory>
#include <span>
#include <vector>

namespace detent::distribution {
using Identity = std::array<char, 16>;
using ScopeIdentity = std::array<char, 32>;
enum class UpdateKind : std::uint8_t { Snapshot = 1, Delta = 2, Progress = 3 };
struct Cursor {
	Identity cluster{}, group{}, restore_epoch{};
	ScopeIdentity scope{};
	std::uint64_t revision = 0;
	bool operator==(const Cursor &) const = default;
	bool same_identity(const Cursor &other) const noexcept {
		return cluster == other.cluster && group == other.group && restore_epoch == other.restore_epoch &&
			   scope == other.scope;
	}
};
struct Profile {
	std::uint32_t semantic_id = 1;
	std::uint64_t required_capabilities = 1;
	bool operator==(const Profile &) const = default;
};
struct Limits {
	std::uint64_t bytes = 256ull * 1024 * 1024, string_bytes = 128ull * 1024 * 1024;
	std::uint32_t records = 500000, namespaces = 4096, overrides_per_definition = 256;
	std::uint32_t definition_bytes = 16 * 1024;
};
struct WorkBudget {
	std::size_t bytes = 8192, tokens = 128;
};
struct NamespaceRevision {
	std::string name;
	std::uint64_t revision = 0;
};
struct Record {
	std::uint32_t namespace_index = 0;
	std::string key;
	std::uint64_t revision = 0;
	KnobDef definition;
};
class SnapshotDecoder;
// Only a complete semantic decoder mints this capability. Records contain
// subscriber definitions only, never producer receipts or Raft recovery state.
class ValidatedSnapshot {
  public:
	const Cursor &cursor() const noexcept { return cursor_; }
	const Profile &profile() const noexcept { return profile_; }
	const std::deque<NamespaceRevision> &namespaces() const noexcept { return namespaces_; }
	const std::deque<Record> &records() const noexcept { return records_; }
	bool discard(WorkBudget &budget);

  private:
	ValidatedSnapshot() = default;
	Cursor cursor_;
	Profile profile_;
	std::deque<NamespaceRevision> namespaces_;
	std::deque<Record> records_;
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
	std::unique_ptr<Impl> impl_;
};

struct Mutation {
	std::uint32_t namespace_index = 0;
	std::string key;
	std::optional<KnobDef> value; // absent means delete live definition.
};
struct Transaction {
	std::uint64_t revision = 0;
	std::deque<NamespaceRevision> touched;
	std::deque<Mutation> mutations;
};
class DeltaDecoder;
class ValidatedDelta {
  public:
	const Cursor &base() const noexcept { return base_; }
	const Cursor &target() const noexcept { return target_; }
	const Profile &profile() const noexcept { return profile_; }
	UpdateKind kind() const noexcept { return kind_; }
	const std::deque<Transaction> &transactions() const noexcept { return transactions_; }
	bool discard(WorkBudget &budget);

  private:
	ValidatedDelta() = default;
	Cursor base_, target_;
	Profile profile_;
	UpdateKind kind_ = UpdateKind::Delta;
	std::deque<Transaction> transactions_;
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
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::distribution
