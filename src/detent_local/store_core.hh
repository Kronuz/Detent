#pragma once
#include "detent_local/collector.hh"
#include "detent_local/disk_store.hh"
#include "leased_paged_root.hh"
#include "paged_certificate.hh"
#include "paged_publication_artifacts.hh"
namespace detent::local::detail {
struct BootstrapCommitment {
	std::shared_ptr<void> caller;
	DiskUsage usage;
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
	distribution::RetirementHandle<PagedPublicationArtifacts> pending_paged_recovery;
	std::shared_ptr<BootstrapCommitment> bootstrap;
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<CollectorControls> controls;
	StorageIdentity identity{};
	DirectoryIdentity directory_identity{};
	SelectionSnapshot selection{SelectionKnowledge::Uncertain, {}};
	std::optional<Descriptor> verified;
	distribution::RetirementHandle<LeasedPagedRoot> verified_paged;
	static constexpr std::size_t certificate_capacity = 64;
	std::array<distribution::RetirementHandle<PagedDependencyCertificate>, certificate_capacity> certificates;
	std::size_t certificate_position = 0;
	void drop_certificates() noexcept {
		for (auto &entry : certificates)
			entry.reset();
	}
	const PagedDependencyCertificate *certificate(const Descriptor &descriptor) const {
		for (const auto &entry : certificates)
			if (entry && entry->descriptor() == descriptor)
				return entry.get();
		return nullptr;
	}
	void cache_recovered(const RecoveredGeneration &generation) {
		if (!generation.paged_ || certificate(generation.descriptor()))
			return;
		try {
			auto candidate = factory->make<PagedDependencyCertificate>(account, *factory);
			candidate->capture(generation, verified ? certificate(*verified) : nullptr);
			certificates[certificate_position] = std::move(candidate);
		} catch (const distribution::StoragePressure &) {
			certificates[certificate_position].reset();
		} catch (const std::bad_alloc &) {
			certificates[certificate_position].reset();
		}
		certificate_position = (certificate_position + 1) % certificates.size();
	}
	void retain_recovered(const RecoveredGeneration &generation) {
		cache_recovered(generation);
		verified = generation.descriptor();
		verified_paged = generation.paged_;
	}
	void confirm_paged(const RecoveredGeneration &generation) {
		const auto &plan = pending_paged_recovery->plan();
		if (!generation.paged_ || generation.paged_->storage_identity() != identity ||
			paged::encode_root(generation.paged_->root()) != paged::encode_root(plan.root()))
			throw std::invalid_argument("paged recovered graph differs from publication plan");
		const auto counts = generation.paged_->counts(), expected = plan.counts();
		if (counts.bools != expected.bools || counts.ints != expected.ints ||
			counts.overrides != expected.overrides || counts.strings != expected.strings ||
			counts.maximum_overrides != expected.maximum_overrides)
			throw std::invalid_argument("paged recovered counts differ from publication plan");
		retain_recovered(generation);
	}
	std::uint64_t owner = 0;
	StoreStatus status = StoreStatus::Starting;
	std::exception_ptr failure;
	bool closing = false, collect = false;
	std::optional<GenerationId> active_generation;
	void fence(std::exception_ptr error) noexcept {
		if (!failure)
			failure = error;
		ledger.fence();
		status = StoreStatus::Fenced;
	}
};
} // namespace detent::local::detail
