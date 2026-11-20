#pragma once
#include "detent_local/agent_store.hh"
#include "detent_local/disk_store.hh"
#include "leased_paged_root.hh"
#include "paged_publication_artifacts.hh"
#include "paged_root_prefix.hh"
#include "recovery_context.hh"
#include <algorithm>
#include <map>
namespace detent::local::detail {
// A test's hook on each store's PosixIO before the store opens it: completion's sync seam.
struct DiskStoreTesting {
	static void install(void (*)(kronuz::io::completion::PosixIO &) noexcept) noexcept;
};
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
	// Per namespace (D8): a published generation awaiting its confirmed recovery, the attempt
	// recovering it, and the pages its last confirmed recovery verified.
	struct Namespace {
		distribution::RetirementHandle<PagedPublicationArtifacts> pending;
		const MappedRecoveryAdmission *attempt = nullptr;
		distribution::RetirementHandle<LeasedPagedRoot> verified;
	};
	std::map<std::string, Namespace, std::less<>> namespaces;
	bool pending_recovery() const noexcept {
		return std::any_of(namespaces.begin(), namespaces.end(), [](const auto &entry) { return bool(entry.second.pending); });
	}
	// The one writer of the directory (D8). Disk work touches it off the owner, one job at a time.
	std::unique_ptr<AgentStore> agent;
	SyncCounters synced, startup_sync, publication_sync;
	DirectoryIdentity directory_identity{};
	SelectionSnapshot selection{SelectionKnowledge::Uncertain, {}, {}, {}};
	void retain_recovered(const std::string &name, const RecoveredGeneration &generation) {
		namespaces[name].verified = generation.paged_;
	}
	void confirm_paged(const std::string &name, const RecoveredGeneration &generation) {
		const auto &plan = namespaces.at(name).pending->plan();
		if (!generation.paged_ ||
			paged::encode_root(generation.paged_->root(), paged_format_limits()) != paged::encode_root(plan.root(), paged_format_limits()))
			throw std::invalid_argument("paged recovered graph differs from publication plan");
		const auto counts = generation.paged_->counts(), expected = plan.counts();
		if (counts.records != expected.records ||
			counts.overrides != expected.overrides || counts.strings != expected.strings ||
			counts.maximum_overrides != expected.maximum_overrides)
			throw std::invalid_argument("paged recovered counts differ from publication plan");
		retain_recovered(name, generation);
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
