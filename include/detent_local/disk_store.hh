#pragma once
#include "builder.hh"
#include "detent_distribution/retirement.hh"
#include "disk_ledger.hh"
#include "identity_reset.hh"
#include "sync_counters.hh"
#include <chrono>
#include <filesystem>
#include <memory>

namespace detent::local {
enum class StoreOpenMode { FreshEmpty, ExistingManaged };
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
	unsigned artifact_mode = 0600;
	ViewLimits recovery_limits;
};
// The store's disk work (a publication or a collection): run() once, on any thread but never beside
// another work of the same store, then advance() on the owner settles it.
class DiskWork {
  public:
	virtual ~DiskWork() = default;
	virtual void run() noexcept = 0;
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
	explicit DiskStore(FactoryTag, std::shared_ptr<detail::StoreCore> core);
	~DiskStore();
	DiskStore(const DiskStore &) = delete;
	StoreStatus status() const noexcept;
	DiskStats stats() const noexcept;
	SyncStats sync_stats() const noexcept;
	SelectionSnapshot selection() const noexcept;
	DirectoryIdentity directory_identity() const noexcept;
	// The next disk work, or null. Taking it starts it: the store waits for its run() before any other.
	std::shared_ptr<DiskWork> work();
	void advance();
	void request_collection() noexcept;
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
