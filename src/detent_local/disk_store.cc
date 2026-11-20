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
void (*io_hook)(c::PosixIO &) noexcept = nullptr;
DiskUsage census(const StoreUsage &usage) noexcept { return {usage.bytes, usage.entries, usage.generations}; }
// One cycle or one collection on the agent store. run() touches only the store and this work.
struct AgentWork final : DiskWork {
	AgentStore *agent = nullptr;
	bool collect = false, deferred = false;
	std::vector<PreparedGeneration> cycle;
	std::vector<std::string> dropped;
	std::optional<DirectoryCursor> directory;
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
				agent->publish(directory.value_or(agent->table().directory), cycle, dropped);
		} catch (...) {
			error = std::current_exception();
		}
		usage = agent->usage();
		// A generation the table does not reach survives a collection only under a reader's lease.
		deferred = collect && usage.generations > agent->table().entries.size();
		synced = agent->sync_counters();
		done.store(true, std::memory_order_release);
	}
};
} // namespace

void detail::DiskStoreTesting::install(void (*hook)(c::PosixIO &) noexcept) noexcept { io_hook = hook; }

struct DiskStore::Impl {
	explicit Impl(std::shared_ptr<detail::StoreCore> value) : core(std::move(value)) {}
	std::shared_ptr<detail::StoreCore> core;
	// Created and not yet taken, or taken and not yet settled; never both.
	std::shared_ptr<AgentWork> pending, running;
	// What a pending or running publication views, and settles with: per namespace, its artifacts.
	std::vector<std::pair<std::string, distribution::RetirementHandle<detail::PagedPublicationArtifacts>>> artifacts;
	std::shared_ptr<const distribution::OwnedBuffer> image, live;
	std::vector<NamespaceSelection> published;
	DiskLedger::Permit permit;
	SyncCounters before;
	DiskPublicationStatus publication = DiskPublicationStatus::Idle;
	std::exception_ptr publication_error, collection_error;
	// A collection that left a held generation runs again after a backoff (1s doubling to 30s).
	std::chrono::steady_clock::time_point collection_retry = std::chrono::steady_clock::time_point::max();
	std::chrono::seconds collection_backoff{1};

	// Queues one cycle: the namespaces' generations, the entries `dropped` removed.
	void queue(std::vector<PreparedGeneration> generations, std::vector<std::string> dropped,
			   std::optional<DirectoryCursor> directory, DiskUsage peak, bool adopt) {
		auto &core = *this->core;
		auto work = std::make_shared<AgentWork>();
		work->agent = core.agent.get();
		work->dropped = std::move(dropped);
		work->cycle = std::move(generations);
		work->directory = directory;
		permit = core.ledger.reserve(peak);
		// A cycle that drops every other entry may change the store's profile.
		if (adopt)
			core.agent->adopt(published.front().descriptor.profile);
		pending = std::move(work);
		publication = DiskPublicationStatus::Preparing;
		publication_error = {};
	}
	void release_publication() noexcept {
		artifacts.clear();
		image.reset();
		live.reset();
		published.clear();
	}
	void select(const AgentWork &work) {
		auto &selection = core->selection;
		auto &entries = selection.namespaces;
		for (const auto &name : work.dropped) {
			std::erase_if(entries, [&](const auto &entry) { return entry.name == name; });
			core->namespaces.erase(name);
		}
		for (const auto &entry : published) {
			const auto at = std::lower_bound(entries.begin(), entries.end(), entry.name,
											 [](const auto &e, const std::string &key) { return e.name < key; });
			if (at != entries.end() && at->name == entry.name)
				at->descriptor = entry.descriptor;
			else
				entries.insert(at, entry);
		}
		selection.directory = core->agent->table().directory;
		selection.knowledge = entries.empty() ? SelectionKnowledge::Absent : SelectionKnowledge::Known;
		selection.descriptor = entries.size() == 1 ? std::optional(entries.front().descriptor) : std::nullopt;
	}
	void settle(AgentWork &work) {
		auto &core = *this->core;
		core.synced = work.synced;
		if (work.collect) {
			collection_error = work.error;
			if (!work.error)
				core.ledger.observe(census(work.usage));
			if (work.deferred) {
				collection_retry = std::chrono::steady_clock::now() + collection_backoff;
				collection_backoff = std::min(collection_backoff * 2, std::chrono::seconds(30));
			} else {
				collection_retry = std::chrono::steady_clock::time_point::max();
				collection_backoff = std::chrono::seconds(1);
			}
			return;
		}
		if (!work.error) {
			core.ledger.settle(permit, census(work.usage));
			core.publication_sync = work.synced - before;
			select(work);
			for (auto &[name, handle] : artifacts) {
				auto &state = core.namespaces[name];
				state.attempt = handle->recovery_admission().get();
				state.pending = std::move(handle);
			}
			if (artifacts.empty())
				for (const auto &entry : published)
					core.namespaces[entry.name].verified.reset();
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
std::chrono::steady_clock::time_point DiskStore::collection_retry_at() const noexcept {
	return impl_->core->closing ? std::chrono::steady_clock::time_point::max() : impl_->collection_retry;
}
std::shared_ptr<DiskWork> DiskStore::work() {
	auto &core = *impl_->core;
	if (impl_->running)
		return nullptr;
	if (!core.closing && std::chrono::steady_clock::now() >= impl_->collection_retry) {
		core.collect = true;
		impl_->collection_retry = std::chrono::steady_clock::time_point::max();
	}
	if (!impl_->pending && core.collect && !core.closing && core.status == StoreStatus::Ready &&
		!core.pending_recovery()) {
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
		core.namespaces.clear();
		core.factory.reset();
		core.agent.reset();
		core.status = StoreStatus::Closed;
	}
}
void DiskStore::request_collection() noexcept { impl_->core->collect = true; }
bool DiskStore::publication_ready() const noexcept {
	const auto &core = *impl_->core;
	return core.status == StoreStatus::Ready && !core.closing && !core.pending_recovery() && !impl_->pending &&
		   !impl_->running;
}
void DiskStore::begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base) {
	begin_impl(builder, expected_base, nullptr);
}
void DiskStore::begin(detail::PagedPlan &&plan) { begin_paged(std::move(plan), nullptr); }
void DiskStore::begin_reset(detail::PagedPlan &&plan, const TrustedIdentityReset &reset) {
	begin_paged(std::move(plan), &reset);
}
void DiskStore::begin(std::vector<NamespacePublication> &&cycle, std::vector<std::string> dropped,
					  std::optional<distribution::DirectoryCursor> directory) {
	std::vector<std::pair<std::string, detail::PagedPlan *>> plans;
	for (auto &entry : cycle) {
		if (!entry.plan)
			throw std::invalid_argument("managed publication cycle is not one plan per namespace in order");
		plans.emplace_back(entry.name, entry.plan.get());
	}
	begin_cycle(plans, std::move(dropped), directory, nullptr);
}
void DiskStore::begin_paged(detail::PagedPlan &&plan, const TrustedIdentityReset *reset) {
	const auto &core = *impl_->core;
	auto name = namespace_name(plan.descriptor().cursor);
	std::vector<std::string> dropped;
	for (const auto &entry : core.selection.namespaces)
		if (entry.name != name)
			dropped.push_back(entry.name);
	const std::pair<std::string, detail::PagedPlan *> cycle[]{{std::move(name), &plan}};
	begin_cycle(cycle, std::move(dropped), {}, reset);
}
void DiskStore::begin_cycle(std::span<const std::pair<std::string, detail::PagedPlan *>> cycle, std::vector<std::string> dropped,
							std::optional<distribution::DirectoryCursor> directory, const TrustedIdentityReset *reset) {
	auto &core = *impl_->core;
	if (!publication_ready())
		throw std::logic_error("managed store unavailable for publication");
	if (core.selection.knowledge == SelectionKnowledge::Uncertain || (cycle.empty() && dropped.empty() && !directory))
		throw std::invalid_argument("managed paged publication actual selection mismatch");
	for (std::size_t i = 0; i < cycle.size(); ++i)
		if (!cycle[i].second || (i && !(cycle[i - 1].first < cycle[i].first)) ||
			std::find(dropped.begin(), dropped.end(), cycle[i].first) != dropped.end())
			throw std::invalid_argument("managed publication cycle is not one plan per namespace in order");
	const auto &limits = core.options.recovery_limits;
	std::vector<distribution::RetirementHandle<detail::PagedPublicationArtifacts>> staged;
	std::vector<PreparedGeneration> generations;
	std::vector<NamespaceSelection> published;
	std::vector<std::uint64_t> packs;
	std::uint64_t live_bytes = 0;
	for (const auto &[name, owned] : cycle) {
		auto &plan = *owned;
		const auto descriptor = plan.descriptor();
		const auto base = plan.selected_base();
		const auto intent = plan.intent();
		const auto *selected = core.selection.find(name);
		const std::optional<Descriptor> actual = selected ? std::optional(*selected) : std::nullopt;
		// A fresh build replaces an absent entry or one of another incarnation (W7).
		if (actual != base && !(intent == detail::PagedPlanIntent::Fresh && !base && actual &&
								!actual->cursor.same_identity(descriptor.cursor)))
			throw std::invalid_argument("managed paged publication actual selection mismatch");
		if (intent != detail::PagedPlanIntent::Fresh && !actual)
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
		const auto found = core.namespaces.find(name);
		const auto verified = plan.snapshot() || found == core.namespaces.end()
								  ? distribution::RetirementHandle<detail::LeasedPagedRoot>{}
								  : found->second.verified;
		recovery->populate(plan, verified, limits);
		artifacts->attach_recovery(std::move(recovery));
		// The cycle views the artifacts, which the store keeps until the cycle settles.
		PreparedGeneration generation;
		generation.name = name;
		generation.cursor = namespace_cursor(descriptor.cursor);
		generation.base = plan.snapshot() ? GenerationId{} : base->generation;
		generation.ordinal = descriptor.ordinal;
		std::map<SegmentId, PreparedGeneration::Segment> segments;
		for (const auto &artifact : artifacts->artifacts()) {
			const auto file_name = artifact.filename();
			if (const auto segment = detail::parse_segment_name(file_name)) {
				auto &prepared = segments[segment->segment];
				prepared.id = segment->segment;
				if (segment->kind == detail::SegmentFile::Owner)
					prepared.owner = artifact.bytes();
				else {
					prepared.pack = artifact.bytes();
					packs.push_back(artifact.bytes().size());
				}
			} else if (const auto file = detail::parse_generation_name(file_name);
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
		live_bytes += generation.live.size();
		generations.push_back(std::move(generation));
		published.push_back({name, descriptor});
		staged.push_back(std::move(artifacts));
	}
	const auto peak = DiskLedger::paged_peak(packs, live_bytes);
	// Every other entry dropped: the cycle may change the store's profile.
	const bool adopt = !published.empty() && dropped.size() + published.size() >= core.selection.namespaces.size() &&
					   std::all_of(core.selection.namespaces.begin(), core.selection.namespaces.end(), [&](const auto &e) {
						   return std::find(dropped.begin(), dropped.end(), e.name) != dropped.end() ||
								  std::any_of(published.begin(), published.end(),
											  [&](const auto &p) { return p.name == e.name; });
					   });
	// Queued before the plans move: a refused reservation leaves the caller's plans whole.
	impl_->published = published;
	impl_->queue(std::move(generations), std::move(dropped), directory, peak, adopt);
	try {
		for (std::size_t i = 0; i < cycle.size(); ++i) {
			staged[i]->attach(std::move(*cycle[i].second));
			impl_->artifacts.emplace_back(cycle[i].first, std::move(staged[i]));
		}
	} catch (...) {
		cancel_publication();
		throw;
	}
}
std::vector<std::string> DiskStore::pending() const {
	std::vector<std::string> out;
	for (const auto &[name, state] : impl_->core->namespaces)
		if (state.pending)
			out.push_back(name);
	return out;
}
std::vector<std::string> DiskStore::failed() const {
	const auto &core = *impl_->core;
	if (!core.agent)
		return {};
	const auto names = core.agent->failed();
	return {names.begin(), names.end()};
}
void DiskStore::confirm_recovered(const RecoveredGeneration &generation) {
	const auto names = pending();
	if (names.size() != 1)
		throw std::invalid_argument("paged publication recovery does not match selected state");
	confirm_recovered(names.front(), generation);
}
void DiskStore::confirm_recovered(std::string_view name, const RecoveredGeneration &generation) {
	auto &core = *impl_->core;
	const auto found = core.namespaces.find(name);
	const auto *selected = core.selection.find(name);
	if (found == core.namespaces.end() || !found->second.pending || core.closing ||
		core.status != StoreStatus::Ready || !selected || *selected != generation.descriptor() ||
		generation.directory_identity() != core.directory_identity)
		throw std::invalid_argument("paged publication recovery does not match selected state");
	core.confirm_paged(found->first, generation);
	found->second.pending.reset();
	found->second.attempt = nullptr;
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
	// Charged to the store's account, as a paged publication's artifacts are.
	auto live = distribution::make_owned_buffer(core.account, detail::live_record_size(0));
	live->append(std::string(detail::live_record_size(0), '\0'));
	detail::encode_live_record(record, live->mutable_bytes());
	live->seal();
	PreparedGeneration generation;
	generation.name = namespace_name(descriptor.cursor);
	generation.cursor = namespace_cursor(descriptor.cursor);
	generation.base = full_copy ? GenerationId{} : core.selection.descriptor->generation;
	generation.ordinal = descriptor.ordinal;
	const auto peak = DiskLedger::generation_peak(descriptor.length, 0, live->size());
	std::vector<std::string> dropped;
	for (const auto &entry : core.selection.namespaces)
		if (entry.name != generation.name)
			dropped.push_back(entry.name);
	impl_->published = {{generation.name, descriptor}};
	impl_->live = std::move(live);
	impl_->image = std::move(bytes);
	generation.image = impl_->image->view();
	generation.live = impl_->live->view();
	try {
		std::vector<PreparedGeneration> cycle;
		cycle.push_back(std::move(generation));
		impl_->queue(std::move(cycle), std::move(dropped), {}, peak, true);
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
	if (io_hook)
		io_hook(*io);
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
		// Each entry's descriptor from its root's prefix; generations recover on demand (C2).
		auto &selection = core->selection;
		selection = {SelectionKnowledge::Absent, {}, {}, core->agent->table().directory};
		if (!core->agent->table().entries.empty()) {
			const auto failed = core->agent->failed();
			for (auto &[name, descriptor] : read_generation_selections(directory))
				if (std::find(failed.begin(), failed.end(), name) == failed.end())
					selection.namespaces.push_back({std::move(name), descriptor});
			selection.knowledge = SelectionKnowledge::Known;
			if (selection.namespaces.size() == 1)
				selection.descriptor = selection.namespaces.front().descriptor;
		}
		core->status = StoreStatus::Ready;
	} catch (...) {
		core->fence(std::current_exception());
	}
	return store;
}
detail::ManagedRecoveryContext detail::ManagedRecovery::capture(const DiskStore &store, std::string_view name) {
	const auto &core = *store.impl_->core;
	if (!core.factory || !core.legacy_recovery || core.closing)
		throw std::logic_error("managed recovery context unavailable");
	std::string entry(name);
	if (entry.empty() && core.selection.namespaces.size() == 1)
		entry = core.selection.namespaces.front().name;
	const auto found = core.namespaces.find(entry);
	const detail::StoreCore::Namespace none;
	const auto &state = found == core.namespaces.end() ? none : found->second;
	const bool snapshot = state.pending && state.pending->plan().snapshot();
	RecoveryContext paged{*core.factory,
						  core.account,
						  core.options.recovery_limits.mapping_account,
						  core.options.recovery_limits,
						  snapshot ? decltype(state.verified){} : state.verified,
						  state.pending ? state.pending->recovery_admission()
										: distribution::RetirementHandle<MappedRecoveryAdmission>{}};
	paged.trusted_start = true;
	paged.name = entry;
	auto legacy = *core.legacy_recovery;
	legacy.name = entry;
	return {std::move(legacy), std::move(paged), store.impl_->core, core.directory_identity};
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
	const auto found = core.namespaces.find(context.paged.name);
	if (found == core.namespaces.end() || found->second.attempt != context.paged.admission.get())
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
