#pragma once
#include "detent_local/agent_store.hh"
#include "detent_local/disk_store.hh"
#include "leased_paged_root.hh"
#include "paged_publication_artifacts.hh"
#include "paged_root_prefix.hh"
#include "recovery_context.hh"
namespace detent::local::detail {
struct StoreCore {
	StoreCore(std::shared_ptr<distribution::StorageLedger> account, DiskStoreOptions options,
			  distribution::RetirementFactory allocator)
		: account(std::move(account)), options(options), ledger(options.limits),
		  factory(std::move(allocator)) {}
	std::shared_ptr<distribution::StorageLedger> account;
	DiskStoreOptions options;
	DiskLedger ledger;
	std::optional<distribution::RetirementFactory> factory;
	std::optional<RecoveryContext> legacy_recovery;
	distribution::RetirementHandle<PagedPublicationArtifacts> pending_paged_recovery;
	const MappedRecoveryAdmission *recovery_attempt = nullptr;
	// The one writer of the directory (D8). Disk work touches it off the owner, one job at a time.
	std::unique_ptr<AgentStore> agent;
	SyncCounters synced, startup_sync, publication_sync;
	DirectoryIdentity directory_identity{};
	SelectionSnapshot selection{SelectionKnowledge::Uncertain, {}};
	std::optional<Descriptor> verified;
	distribution::RetirementHandle<LeasedPagedRoot> verified_paged;
	void retain_recovered(const RecoveredGeneration &generation) {
		verified = generation.descriptor();
		verified_paged = generation.paged_;
	}
	void confirm_paged(const RecoveredGeneration &generation) {
		const auto &plan = pending_paged_recovery->plan();
		if (!generation.paged_ ||
			paged::encode_root(generation.paged_->root(), paged_format_limits()) != paged::encode_root(plan.root(), paged_format_limits()))
			throw std::invalid_argument("paged recovered graph differs from publication plan");
		const auto counts = generation.paged_->counts(), expected = plan.counts();
		if (counts.records != expected.records ||
			counts.overrides != expected.overrides || counts.strings != expected.strings ||
			counts.maximum_overrides != expected.maximum_overrides)
			throw std::invalid_argument("paged recovered counts differ from publication plan");
		retain_recovered(generation);
	}
	StoreStatus status = StoreStatus::Starting;
	std::exception_ptr failure;
	bool closing = false, collect = false;
	void fence(std::exception_ptr error) noexcept {
		if (!failure)
			failure = error;
		ledger.fence();
		status = StoreStatus::Fenced;
	}
};
} // namespace detent::local::detail
