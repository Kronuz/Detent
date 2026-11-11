#pragma once
#include "builder.hh"
#include "disk_ledger.hh"
#include "identity_reset.hh"
#include "storage_session.hh"
#include "sync_counters.hh"
#include <chrono>

namespace detent::local {
enum class StoreOpenMode { FreshEmpty, ExistingManaged, UpgradeLegacy };
enum class StoreStatus { Starting, Ready, Fenced, Closing, Closed };
enum class SelectionKnowledge { Absent, Known, Uncertain };
enum class DiskPublicationStatus { Idle, Preparing, Published, Rejected, Cancelled, Fenced };
struct SelectionSnapshot {
	SelectionKnowledge knowledge = SelectionKnowledge::Absent;
	std::optional<Descriptor> descriptor;
};
struct DiskStoreOptions {
	StoreOpenMode mode = StoreOpenMode::ExistingManaged;
	DiskLimits limits;
	std::uint64_t census_work_limit = 1'000'000;
	std::size_t gc_scan_slice = 256;
	unsigned artifact_mode = 0600;
	ViewLimits recovery_limits;
	// A publication writes its files in sync groups of at most this many open descriptors; each group
	// is one SyncFiles (on macOS one device flush). Descriptors are the bound, not the file count.
	std::size_t sync_group_files = 128;
};
namespace detail {
struct StoreCore;
class PagedPlan;
class ManagedRecovery;
} // namespace detail
// Single-executor coordinator. The application retains and drives the store
// through close/drain; accepted drivers retain the entire outer operation.
class DiskStore {
  public:
	class FactoryTag {
		FactoryTag() = default;
		friend std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &,
														  const std::filesystem::path &,
														  std::shared_ptr<void>, DiskStoreOptions);
	};
	DiskStore(FactoryTag, std::shared_ptr<detail::StoreCore> core);
	~DiskStore();
	DiskStore(const DiskStore &) = delete;
	StoreStatus status() const noexcept;
	DiskStats stats() const noexcept;
	SyncStats sync_stats() const noexcept;
	SelectionSnapshot selection() const noexcept;
	DirectoryIdentity directory_identity() const noexcept;
	std::shared_ptr<kronuz::io::completion::Operation> operation() const;
	void advance();
	bool needs_work() const noexcept;
	void request_collection() noexcept;
	// The owner wakes at this deadline to retry conservatively retained roots.
	std::chrono::steady_clock::time_point collection_retry_at() const noexcept;
	// True at a settled scheduling boundary, including paused collection.
	bool publication_ready() const noexcept;
	void begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base = {});
	void begin(detail::PagedPlan &&);
	// Settles the retained plan only after actual selected recovery succeeds.
	void confirm_recovered(const RecoveredGeneration &);
	void begin_reset(const ImageBuilder &builder, const TrustedIdentityReset &reset);
	void begin_reset(detail::PagedPlan &&, const TrustedIdentityReset &);
	void cancel_publication() noexcept;
	DiskPublicationStatus publication_status() const noexcept;
	std::exception_ptr publication_error() const noexcept;
	const std::shared_ptr<distribution::StorageLedger> &storage_account() const noexcept;
	void close() noexcept;
	bool drained() const noexcept;
	std::exception_ptr error() const noexcept;
	std::exception_ptr collection_error() const noexcept;

  private:
	friend class detail::ManagedRecovery;
	void begin_impl(const ImageBuilder &, std::optional<distribution::Cursor>, const TrustedIdentityReset *);
	void begin_paged(detail::PagedPlan &&, const TrustedIdentityReset *);
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
	friend std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &,
													  const std::filesystem::path &, std::shared_ptr<void>,
													  DiskStoreOptions);
};
// Startup/validation worker only: owns locking, entropy and full selected
// semantic recovery. Returned Starting work uses bounded completion requests.
std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &domain,
										   const std::filesystem::path &directory,
										   std::shared_ptr<void> admission, DiskStoreOptions options);
} // namespace detent::local
