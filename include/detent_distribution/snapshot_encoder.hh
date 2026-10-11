#pragma once
#include "buffer.hh"
#include "protocol.hh"
#include "retirement.hh"

namespace detent::distribution {
struct SnapshotRecordStep {
	std::optional<Record> record;
	bool done = false;
};
// A frozen public projection. Each record step performs bounded work, including
// empty namespaces. Overrides are consumed before requesting another record.
class SnapshotSource {
  public:
	virtual ~SnapshotSource() = default;
	virtual const Cursor &cursor() const noexcept = 0;
	virtual std::shared_ptr<StorageLedger> storage_account() const noexcept = 0;
	virtual std::uint64_t namespace_count() const noexcept = 0;
	virtual NamespaceRevision name_space(std::uint64_t) const = 0;
	// Optional bounded preparation completes before counting namespaces.
	virtual bool prepare(WorkBudget &) { return true; }
	virtual void reset_records() noexcept = 0;
	virtual SnapshotRecordStep next_record() = 0;
	virtual std::optional<OverrideRow> next_override() = 0;
	virtual bool discard(WorkBudget &) noexcept { return true; }
};
// Shared inputs are borrowed: retain their storage until staging ends.
std::shared_ptr<SnapshotSource> make_snapshot_source(std::shared_ptr<const ValidatedSnapshot>,
													 std::shared_ptr<StorageLedger> output_account = {});
// Moved input retires on the supplied domain after its final source release.
std::shared_ptr<SnapshotSource> make_snapshot_source(ValidatedSnapshot &&, RetirementDomain &,
													 std::shared_ptr<StorageLedger> output_account = {});
// Count/emission share one immutable source. Only sealed, independently owned
// chunks cross the transport boundary; callers keep source access owner-local.
class SnapshotEncoder {
  public:
	explicit SnapshotEncoder(std::shared_ptr<SnapshotSource>, Limits = {});
	~SnapshotEncoder();
	SnapshotEncoder(SnapshotEncoder &&) noexcept;
	SnapshotEncoder(const SnapshotEncoder &) = delete;
	void step(WorkBudget &);
	bool counted() const noexcept;
	std::uint64_t encoded_length() const;
	bool done() const noexcept;
	bool chunk_ready() const noexcept;
	std::shared_ptr<const OwnedBuffer> take_chunk();
	void discard() noexcept;
	bool discard(WorkBudget &) noexcept;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
