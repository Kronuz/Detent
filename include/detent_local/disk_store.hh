#pragma once
#include "builder.hh"
#include "detent_distribution/retirement.hh"
#include "disk_ledger.hh"
#include "identity_reset.hh"
#include "sync_counters.hh"
#include <chrono>
#include <exception>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace detent::local {
enum class StoreOpenMode { FreshEmpty, ExistingManaged };
enum class StoreStatus { Starting, Ready, Fenced, Closing, Closed };
enum class SelectionKnowledge { Absent, Known, Uncertain };
enum class DiskPublicationStatus { Idle, Preparing, Published, Rejected, Cancelled, Fenced };
struct NamespaceSelection {
	std::string name;
	Descriptor descriptor;
	bool operator==(const NamespaceSelection &) const = default;
};
// The table's entries (D8), one generation per namespace in name order. `descriptor` is the entry
// when the table holds exactly one.
struct SelectionSnapshot {
	SelectionKnowledge knowledge = SelectionKnowledge::Absent;
	std::optional<Descriptor> descriptor;
	std::vector<NamespaceSelection> namespaces;
	distribution::DirectoryCursor directory;
	const Descriptor *find(std::string_view name) const noexcept {
		const auto found = std::lower_bound(namespaces.begin(), namespaces.end(), name,
											[](const auto &entry, std::string_view key) { return entry.name < key; });
		return found != namespaces.end() && found->name == name ? &found->descriptor : nullptr;
	}
};
// One namespace's generation in a publication cycle.
struct NamespacePublication;
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
	// When a collection that left a reader's generation runs again; max() when none waits.
	std::chrono::steady_clock::time_point collection_retry_at() const noexcept;
	// True at a settled scheduling boundary, including paused collection.
	bool publication_ready() const noexcept;
	void begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base = {});
	// The table's only namespace, named by its cursor; every other entry is dropped.
	void begin(detail::PagedPlan &&);
	// One cycle (D8, D11): each namespace's generation, the entries `dropped` removed, the directory
	// cursor recorded. Every plan's base is that namespace's selected generation.
	void begin(std::vector<NamespacePublication> &&, std::vector<std::string> dropped = {},
			   std::optional<distribution::DirectoryCursor> directory = {});
	// Settles a published plan only after actual selected recovery succeeds; one call per namespace
	// the cycle published (`pending()` names them).
	void confirm_recovered(const RecoveredGeneration &);
	void confirm_recovered(std::string_view name, const RecoveredGeneration &);
	std::vector<std::string> pending() const;
	// Entries whose files did not verify at startup and have not been republished (D10).
	std::vector<std::string> failed() const;
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
	void begin_cycle(std::span<const std::pair<std::string, detail::PagedPlan *>>, std::vector<std::string>,
					 std::optional<distribution::DirectoryCursor>, const TrustedIdentityReset *);
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
	friend std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &,
													  const std::filesystem::path &, std::shared_ptr<void>,
													  DiskStoreOptions);
};
namespace detail {
class PagedPlan;
}
struct NamespacePublication {
	std::string name;
	std::unique_ptr<detail::PagedPlan> plan;
};
// Startup/validation worker only: owns locking, entropy and full selected
// semantic recovery. Returned Starting work uses bounded completion requests.
std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &domain,
										   const std::filesystem::path &directory,
										   std::shared_ptr<void> admission, DiskStoreOptions options);
} // namespace detent::local
