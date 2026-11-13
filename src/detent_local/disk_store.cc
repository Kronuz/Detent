#include "detent_local/disk_store.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/store_table.hh"
#include "leased_paged_root.hh"
#include "live_record.hh"
#include "managed_recovery.hh"
#include "names.hh"
#include "paged_publication_artifacts.hh"
#include "resources.hh"
#include "segment_names.hh"
#include "store_core.hh"
#include <atomic>
#include <map>
#include <sha256.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

// DiskStore publishes and collects through the agent store (docs/status/namespace-s7.md "Wiring"): the
// owner prepares each cycle, a worker runs it on the AgentStore, and the owner settles the result.
namespace detent::local {
namespace c = kronuz::io::completion;
namespace {
paged::Digest sha(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	paged::Digest out;
	std::copy(digest.begin(), digest.end(), out.begin());
	return out;
}
// A cycle the store refused before touching the table: the caller retries, or repairs (D10).
bool rejected(std::exception_ptr error) {
	try {
		std::rethrow_exception(error);
	} catch (const ReachedNameMismatch &) {
		return true;
	} catch (const LeaseHeld &) {
		return true;
	} catch (...) {
		return false;
	}
}
DiskUsage census(const StoreUsage &usage) noexcept { return {usage.bytes, usage.entries, usage.generations}; }
// One cycle or one collection on the agent store. run() touches only the store and this work.
struct AgentWork final : DiskWork {
	AgentStore *agent = nullptr;
	bool collect = false;
	std::vector<PreparedGeneration> cycle;
	std::vector<std::string> dropped;
	std::exception_ptr error;
	StoreUsage usage;
	SyncCounters synced;
	std::atomic<bool> started{false}, done{false};
	void run() noexcept override {
		if (started.exchange(true, std::memory_order_acq_rel))
			std::terminate();
		try {
			if (collect)
				agent->collect();
			else
				agent->publish({}, cycle, dropped);
		} catch (...) {
			error = std::current_exception();
		}
		usage = agent->usage();
		synced = agent->sync_counters();
		done.store(true, std::memory_order_release);
	}
};
} // namespace

struct DiskStore::Impl {
	explicit Impl(std::shared_ptr<detail::StoreCore> value) : core(std::move(value)) {}
	std::shared_ptr<detail::StoreCore> core;
	// Created and not yet taken, or taken and not yet settled; never both.
	std::shared_ptr<AgentWork> pending, running;
	// What a pending or running publication views, and settles with.
	distribution::RetirementHandle<detail::PagedPublicationArtifacts> artifacts;
	std::shared_ptr<const distribution::OwnedBuffer> image;
	std::string live;
	Descriptor descriptor;
	DiskLedger::Permit permit;
	SyncCounters before;
	DiskPublicationStatus publication = DiskPublicationStatus::Idle;
	std::exception_ptr publication_error, collection_error;

	// Queues a cycle publishing `generation` as the store's one namespace (D88), every other entry dropped.
	void queue(PreparedGeneration generation, DiskUsage peak) {
		auto &core = *this->core;
		if (descriptor.profile != core.agent->table().profile)
			throw std::invalid_argument("managed publication profile differs from the store's");
		auto work = std::make_shared<AgentWork>();
		work->agent = core.agent.get();
		for (const auto &entry : core.agent->table().entries)
			if (entry.name != generation.name)
				work->dropped.push_back(entry.name);
		work->cycle.push_back(std::move(generation));
		permit = core.ledger.reserve(peak);
		pending = std::move(work);
		publication = DiskPublicationStatus::Preparing;
		publication_error = {};
	}
	void release_publication() noexcept {
		artifacts.reset();
		image.reset();
		live.clear();
	}
	void settle(AgentWork &work) {
		auto &core = *this->core;
		core.synced = work.synced;
		if (work.collect) {
			collection_error = work.error;
			if (!work.error)
				core.ledger.observe(census(work.usage));
			return;
		}
		if (!work.error) {
			core.ledger.settle(permit, census(work.usage));
			core.publication_sync = work.synced - before;
			core.selection = {SelectionKnowledge::Known, descriptor};
			if (artifacts) {
				core.pending_paged_recovery = artifacts;
				core.recovery_attempt = artifacts->recovery_admission().get();
			} else
				core.verified_paged.reset();
			core.collect = true;
			publication = DiskPublicationStatus::Published;
		} else if (rejected(work.error)) {
			core.ledger.settle(permit, census(work.usage));
			publication_error = work.error;
			publication = DiskPublicationStatus::Rejected;
		} else {
			core.ledger.settle(permit);
			publication_error = work.error;
			core.fence(work.error);
			publication = DiskPublicationStatus::Fenced;
		}
		release_publication();
	}
};

DiskStore::DiskStore(FactoryTag, std::shared_ptr<detail::StoreCore> core)
	: impl_(distribution::make_storage_owner<Impl>(core->account, std::malloc, std::free, core)) {}
DiskStore::~DiskStore() = default;
StoreStatus DiskStore::status() const noexcept { return impl_->core->status; }
DiskStats DiskStore::stats() const noexcept { return impl_->core->ledger.stats(); }
SyncStats DiskStore::sync_stats() const noexcept {
	const auto &core = *impl_->core;
	return {core.synced, core.startup_sync, core.publication_sync};
}
SelectionSnapshot DiskStore::selection() const noexcept { return impl_->core->selection; }
DirectoryIdentity DiskStore::directory_identity() const noexcept { return impl_->core->directory_identity; }
std::exception_ptr DiskStore::error() const noexcept { return impl_->core->failure; }
std::exception_ptr DiskStore::collection_error() const noexcept { return impl_->collection_error; }
std::shared_ptr<DiskWork> DiskStore::work() {
	auto &core = *impl_->core;
	if (impl_->running)
		return nullptr;
	if (!impl_->pending && core.collect && !core.closing && core.status == StoreStatus::Ready &&
		!core.pending_paged_recovery) {
		auto work = std::make_shared<AgentWork>();
		work->agent = core.agent.get();
		work->collect = true;
		impl_->pending = std::move(work);
		core.collect = false;
	}
	if (!impl_->pending)
		return nullptr;
	if (!impl_->pending->collect)
		impl_->before = core.synced;
	impl_->running = std::move(impl_->pending);
	return impl_->running;
}
void DiskStore::advance() {
	auto &core = *impl_->core;
	if (impl_->running && impl_->running->done.load(std::memory_order_acquire)) {
		const auto work = std::move(impl_->running);
		try {
			impl_->settle(*work);
		} catch (...) {
			core.fence(std::current_exception());
		}
	}
	if (core.closing && !impl_->running && core.status != StoreStatus::Closed) {
		cancel_publication();
		impl_->pending.reset();
		core.pending_paged_recovery.reset();
		core.verified_paged.reset();
		core.factory.reset();
		core.agent.reset();
		core.status = StoreStatus::Closed;
	}
}
void DiskStore::request_collection() noexcept { impl_->core->collect = true; }
bool DiskStore::publication_ready() const noexcept {
	const auto &core = *impl_->core;
	return core.status == StoreStatus::Ready && !core.closing && !core.pending_paged_recovery && !impl_->pending &&
		   !impl_->running;
}
void DiskStore::begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base) {
	begin_impl(builder, expected_base, nullptr);
}
void DiskStore::begin(detail::PagedPlan &&plan) { begin_paged(std::move(plan), nullptr); }
void DiskStore::begin_reset(detail::PagedPlan &&plan, const TrustedIdentityReset &reset) {
	begin_paged(std::move(plan), &reset);
}
void DiskStore::begin_paged(detail::PagedPlan &&plan, const TrustedIdentityReset *reset) {
	auto &core = *impl_->core;
	if (!publication_ready())
		throw std::logic_error("managed store unavailable for publication");
	const auto descriptor = plan.descriptor();
	const auto base = plan.selected_base();
	const auto intent = plan.intent();
	if (core.selection.knowledge == SelectionKnowledge::Uncertain || core.selection.descriptor != base ||
		(intent == detail::PagedPlanIntent::Fresh ? core.selection.knowledge != SelectionKnowledge::Absent
												  : core.selection.knowledge != SelectionKnowledge::Known))
		throw std::invalid_argument("managed paged publication actual selection mismatch");
	if (reset) {
		if (intent != detail::PagedPlanIntent::TrustedReset || !base || *base != reset->expected_selected ||
			!descriptor.cursor.same_identity(reset->configured) || descriptor.profile != reset->profile ||
			base->cursor.same_identity(descriptor.cursor) || plan.registry())
			throw std::invalid_argument("managed paged trusted identity reset mismatch");
	} else if (intent == detail::PagedPlanIntent::TrustedReset ||
			   (base && (!descriptor.cursor.same_identity(base->cursor) || descriptor.profile != base->profile ||
						 (plan.maintenance() ? descriptor.cursor != base->cursor
						  : intent == detail::PagedPlanIntent::Repair
							  ? descriptor.cursor.revision < base->cursor.revision
							  : descriptor.cursor.revision <= base->cursor.revision))))
		throw std::invalid_argument("managed paged publication requires authorized intent");
	const auto &limits = core.options.recovery_limits;
	if (plan.directory_identity() != core.directory_identity || plan.bytes()->account() != core.account ||
		descriptor.length != paged::root_bytes || descriptor.length > limits.bytes ||
		plan.root().bytes > limits.bytes - descriptor.length || plan.root().pages > limits.pages ||
		plan.root().segments > limits.segments || plan.root().records > limits.records ||
		plan.root().namespaces > limits.namespaces || plan.counts().strings > limits.strings ||
		plan.counts().maximum_overrides > limits.overrides)
		throw std::invalid_argument("managed paged publication exact base or admission mismatch");
	if (next_generation(base, descriptor.cursor, descriptor.profile, plan.snapshot()) !=
		DerivedGeneration{descriptor.generation, descriptor.ordinal})
		throw std::invalid_argument("managed paged publication name is not derived");
	auto artifacts = core.factory->make<detail::PagedPublicationArtifacts>(core.account);
	artifacts->populate(plan);
	if (artifacts->target_physical_bytes() > limits.bytes)
		throw std::invalid_argument("paged target packs exceed recovery byte limit");
	auto recovery = core.factory->make<detail::MappedRecoveryAdmission>(core.account, limits.mapping_account,
																		  *core.factory);
	// A snapshot shares no packs with its base, which may be damaged.
	recovery->populate(plan, plan.snapshot() ? decltype(core.verified_paged){} : core.verified_paged, limits);
	artifacts->attach_recovery(std::move(recovery));
	// The cycle views the artifacts, which the store keeps until the cycle settles.
	PreparedGeneration generation;
	generation.name = namespace_name(descriptor.cursor);
	generation.cursor = namespace_cursor(descriptor.cursor);
	generation.base = plan.snapshot() ? GenerationId{} : base->generation;
	generation.ordinal = descriptor.ordinal;
	std::map<SegmentId, PreparedGeneration::Segment> segments;
	std::vector<std::uint64_t> packs;
	for (const auto &artifact : artifacts->artifacts()) {
		const auto name = artifact.filename();
		if (const auto segment = detail::parse_segment_name(name)) {
			auto &prepared = segments[segment->segment];
			prepared.id = segment->segment;
			if (segment->kind == detail::SegmentFile::Owner)
				prepared.owner = artifact.bytes();
			else {
				prepared.pack = artifact.bytes();
				packs.push_back(artifact.bytes().size());
			}
		} else if (const auto file = detail::parse_generation_name(name);
				   file && file->generation == descriptor.generation) {
			if (file->kind == detail::GenerationFile::Image)
				generation.image = artifact.bytes();
			else if (file->kind == detail::GenerationFile::ReaderMetadata)
				generation.read = artifact.bytes();
			else if (file->kind == detail::GenerationFile::Live)
				generation.live = artifact.bytes();
			else
				throw std::logic_error("unexpected paged publication artifact");
		} else
			throw std::logic_error("unexpected paged publication artifact");
	}
	for (const auto &[id, segment] : segments)
		generation.segments.push_back(segment);
	const auto peak = DiskLedger::paged_peak(packs, generation.live.size());
	artifacts->attach(std::move(plan));
	impl_->descriptor = descriptor;
	impl_->queue(std::move(generation), peak);
	impl_->artifacts = std::move(artifacts);
}
void DiskStore::confirm_recovered(const RecoveredGeneration &generation) {
	auto &core = *impl_->core;
	if (!core.pending_paged_recovery || core.closing || core.status != StoreStatus::Ready ||
		core.selection.knowledge != SelectionKnowledge::Known ||
		core.selection.descriptor != generation.descriptor() ||
		generation.directory_identity() != core.directory_identity)
		throw std::invalid_argument("paged publication recovery does not match selected state");
	core.confirm_paged(generation);
	core.pending_paged_recovery.reset();
	core.recovery_attempt = nullptr;
	advance();
}
void DiskStore::begin_reset(const ImageBuilder &builder, const TrustedIdentityReset &reset) {
	begin_impl(builder, reset.expected_selected.cursor, &reset);
}
void DiskStore::begin_impl(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base,
						   const TrustedIdentityReset *reset) {
	auto &core = *impl_->core;
	if (!publication_ready())
		throw std::logic_error("managed store unavailable for publication");
	const auto descriptor = builder.descriptor();
	auto bytes = builder.bytes();
	const auto base =
		core.selection.descriptor ? std::optional(core.selection.descriptor->cursor) : std::nullopt;
	if (core.selection.knowledge == SelectionKnowledge::Uncertain || expected_base != base)
		throw std::invalid_argument("managed publication actual base mismatch");
	if (const auto pinned = builder.base_descriptor();
		pinned && (core.selection.descriptor != pinned || builder.base_directory_identity() != core.directory_identity))
		throw std::invalid_argument("managed publication pinned generation mismatch");
	if (reset) {
		if (core.selection.knowledge != SelectionKnowledge::Known ||
			core.selection.descriptor != reset->expected_selected || builder.base_descriptor() ||
			!descriptor.cursor.same_identity(reset->configured) || descriptor.profile != reset->profile ||
			reset->expected_selected.cursor.same_identity(descriptor.cursor))
			throw std::invalid_argument("managed publication trusted identity reset mismatch");
	} else if (base && (!base->same_identity(descriptor.cursor) || descriptor.cursor.revision <= base->revision))
		throw std::invalid_argument("managed publication requires a forward scoped cursor");
	if (bytes->account() != core.account || bytes->size() != descriptor.length ||
		descriptor.length > core.options.recovery_limits.bytes)
		throw std::invalid_argument("managed image account or extent mismatch");
	const bool full_copy = !builder.base_descriptor();
	if (next_generation(core.selection.descriptor, descriptor.cursor, descriptor.profile, full_copy) !=
		DerivedGeneration{descriptor.generation, descriptor.ordinal})
		throw std::invalid_argument("managed publication name is not derived");
	// A flat image has no `.read` and reaches no segment: an empty record, and a `.live` without rows.
	detail::LiveRecord record;
	record.generation = descriptor.generation;
	std::copy(descriptor.checksum.begin(), descriptor.checksum.end(), record.root_checksum.begin());
	record.read_digest = sha({});
	std::string live(detail::live_record_size(0), '\0');
	detail::encode_live_record(record, live);
	PreparedGeneration generation;
	generation.name = namespace_name(descriptor.cursor);
	generation.cursor = namespace_cursor(descriptor.cursor);
	generation.base = full_copy ? GenerationId{} : core.selection.descriptor->generation;
	generation.ordinal = descriptor.ordinal;
	const auto peak = DiskLedger::generation_peak(descriptor.length, 0, live.size());
	impl_->descriptor = descriptor;
	impl_->live = std::move(live);
	impl_->image = std::move(bytes);
	generation.image = impl_->image->view();
	generation.live = impl_->live;
	try {
		impl_->queue(std::move(generation), peak);
	} catch (...) {
		impl_->release_publication();
		throw;
	}
}
void DiskStore::cancel_publication() noexcept {
	if (!impl_->pending || impl_->pending->collect)
		return;
	impl_->pending.reset();
	impl_->core->ledger.settle(impl_->permit);
	impl_->release_publication();
	impl_->publication = DiskPublicationStatus::Cancelled;
}
DiskPublicationStatus DiskStore::publication_status() const noexcept { return impl_->publication; }
std::exception_ptr DiskStore::publication_error() const noexcept { return impl_->publication_error; }
const std::shared_ptr<distribution::StorageLedger> &DiskStore::storage_account() const noexcept {
	return impl_->core->account;
}
void DiskStore::close() noexcept {
	impl_->core->closing = true;
	if (impl_->core->status != StoreStatus::Closed)
		impl_->core->status = StoreStatus::Closing;
	advance();
}
bool DiskStore::drained() const noexcept { return status() == StoreStatus::Closed; }
std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &domain,
										   const std::filesystem::path &directory,
										   std::shared_ptr<void> admission, DiskStoreOptions options) {
	if (!admission)
		throw std::invalid_argument("invalid managed store admission");
	auto account = domain.storage_account();
	auto &legacy_domain = resources::domain(options.recovery_limits);
	detail::RecoveryContext legacy{legacy_domain.factory(),
								   legacy_domain.storage_account(),
								   resources::mapping_account(options.recovery_limits),
								   options.recovery_limits,
								   {},
								   {}};
	legacy.limits.retirement_domain = nullptr;
	legacy.limits.mapping_account = legacy.mappings;
	options.recovery_limits.retirement_domain = nullptr;
	options.recovery_limits.mapping_account = legacy.mappings;
	auto core = std::allocate_shared<detail::StoreCore>(
		distribution::StorageAllocator<detail::StoreCore>(account), account, options, domain.factory());
	core->legacy_recovery.emplace(std::move(legacy));
	auto store = std::allocate_shared<DiskStore>(distribution::StorageAllocator<DiskStore>(account),
												 DiskStore::FactoryTag{}, core);
	auto io = std::make_shared<c::PosixIO>(directory, options.artifact_mode);
	struct stat info{};
	if (::fstat(io->native_directory_handle(), &info) != 0)
		throw std::system_error(errno, std::generic_category(), "managed store directory inspection");
	core->directory_identity = {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)};
	if (options.mode == StoreOpenMode::FreshEmpty) {
		if (io->scan_directory()->next())
			throw std::runtime_error("fresh storage directory contains existing entries");
	} else if (!io->inspect_entry("owner.lock"))
		throw std::runtime_error("managed store is missing: initialize it first");
	// The store keeps the profile its table holds; a new store takes the default one.
	AgentStore::Options agent{};
	agent.mode = options.artifact_mode;
	if (io->inspect_entry("selected")) {
		const auto file = io->open_existing_read_only("selected");
		const auto size = file->size();
		if (size > table_bytes(1u << 20))
			throw StoreRefused("agent store table unreadable");
		std::string bytes(size, '\0');
		for (std::size_t done = 0; done < bytes.size();) {
			const auto got = file->read_at(done, std::span<char>(bytes).subspan(done));
			if (!got)
				throw StoreRefused("agent store table unreadable");
			done += got;
		}
		agent.profile = decode_table(bytes).profile;
	}
	core->agent = std::make_unique<AgentStore>(io, agent);
	core->synced = core->startup_sync = core->agent->sync_counters();
	core->ledger.initialize(census(core->agent->usage()));
	try {
		const auto &table = core->agent->table();
		if (table.entries.size() > 1)
			throw std::runtime_error("managed store holds more than one namespace");
		if (table.entries.empty())
			core->selection = {SelectionKnowledge::Absent, {}};
		else {
			auto recovered = detail::ManagedRecovery::recover(directory, detail::ManagedRecovery::capture(*store));
			if (recovered.directory_identity() != core->directory_identity)
				throw std::runtime_error("recovered directory differs from the store's");
			core->retain_recovered(recovered);
			core->selection = {SelectionKnowledge::Known, recovered.descriptor()};
		}
		core->status = StoreStatus::Ready;
	} catch (...) {
		core->fence(std::current_exception());
	}
	return store;
}
detail::ManagedRecoveryContext detail::ManagedRecovery::capture(const DiskStore &store) {
	const auto &core = *store.impl_->core;
	if (!core.factory || !core.legacy_recovery || core.closing)
		throw std::logic_error("managed recovery context unavailable");
	const bool snapshot = core.pending_paged_recovery && core.pending_paged_recovery->plan().snapshot();
	RecoveryContext paged{*core.factory,
						  core.account,
						  core.options.recovery_limits.mapping_account,
						  core.options.recovery_limits,
						  snapshot ? decltype(core.verified_paged){} : core.verified_paged,
						  core.pending_paged_recovery
							  ? core.pending_paged_recovery->recovery_admission()
							  : distribution::RetirementHandle<MappedRecoveryAdmission>{}};
	paged.trusted_start = true;
	return {*core.legacy_recovery, std::move(paged), store.impl_->core, core.directory_identity};
}
RecoveredGeneration detail::ManagedRecovery::recover(const std::filesystem::path &directory,
													 ManagedRecoveryContext context) {
	return RecoveryEngine::recover_routed(directory, std::move(context.legacy), std::move(context.paged));
}
void detail::ManagedRecovery::settle_failure(DiskStore &store, const ManagedRecoveryContext &context,
											 std::exception_ptr failure) {
	if (context.owner.lock() != store.impl_->core || !failure)
		throw std::invalid_argument("invalid managed recovery failure settlement");
	if (!context.paged.admission)
		return;
	auto &core = *store.impl_->core;
	if (core.recovery_attempt != context.paged.admission.get())
		throw std::invalid_argument("managed recovery failure does not match pending attempt");
	if (auto original = context.paged.admission->failure())
		failure = original;
	if (core.closing) {
		if (!core.failure)
			core.failure = failure;
		core.ledger.fence();
		return;
	}
	core.fence(failure);
	store.close();
}
} // namespace detent::local
