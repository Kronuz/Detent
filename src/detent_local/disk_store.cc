#include "detent_local/disk_store.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/collector.hh"
#include "detent_local/inventory.hh"
#include "detent_local/record_reader.hh"
#include "leased_paged_root.hh"
#include "names.hh"
#include "paged_certificate.hh"
#include "paged_dependency_check.hh"
#include "paged_navigation.hh"
#include "paged_publication_artifacts.hh"
#include "paged_root_prefix.hh"
#include "segment_names.hh"
#include <completion/image.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::local {
namespace c = kronuz::io::completion;
namespace detail {
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
} // namespace detail
namespace {
bool safe(const c::EntryInspection &entry, const detail::StoreCore &core) noexcept {
	return entry.footprint.kind == c::EntryKind::Regular && entry.links == 1 && entry.owner == core.owner &&
		   entry.device == core.session->directory_identity().device && !(entry.permissions & 0022);
}
StorageIdentity fresh_identity() {
	StorageIdentity value{};
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		int result;
		do {
			result = ::getentropy(value.data(), value.size());
		} while (result < 0 && errno == EINTR);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), "storage identity entropy");
		if (value != StorageIdentity{})
			return value;
	}
	throw std::runtime_error("empty storage identity entropy");
}
struct StartupData {
	explicit StartupData(std::shared_ptr<detail::StoreCore> owner)
		: core(std::move(owner)), session(core->session), controls(core->controls) {}
	std::shared_ptr<detail::StoreCore> core;
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<CollectorControls> controls;
	RecordReader reader;
	std::array<char, 256> name{};
	std::size_t name_size = 0;
	std::array<char, storage_identity_bytes> metadata{};
	GenerationOwnership selected_ownership;
	std::optional<SegmentOwnership> expected_segment;
	std::uint64_t segment_owners_seen = 0, segment_packs_seen = 0;
	c::EntryInspection observed;
	std::optional<Descriptor> selected;
	std::optional<StorageIdentity> stored_identity;
	DiskLedger::Permit metadata_permit;
	std::uint64_t anchors = 0;
	bool owner_seen = false, metadata_seen = false, selected_seen = false, selected_lease_seen = false;
	bool upgrade_blocked = false, scratch_open = false;
	bool selected_owned = false;
	bool selected_image_seen = false;
	std::exception_ptr failure;
	std::string_view observed_name() const noexcept { return {name.data(), name_size}; }
};
class StartupOperation final : public c::Operation {
  public:
	StartupOperation(std::shared_ptr<StartupData> data, std::shared_ptr<InventoryOperation> inventory,
					 std::shared_ptr<c::ImagePreparation> metadata)
		: data_(std::move(data)), inventory_(std::move(inventory)), metadata_(std::move(metadata)),
		  token_(data_->core->session->next_operation()) {}
	const c::Request &request() const override {
		if (phase_ == Phase::Inventory)
			return inventory_->request();
		if (phase_ == Phase::CreateMetadata)
			return metadata_->request();
		return request_;
	}
	void submitted() override {
		if (done() || submitted_)
			throw std::logic_error("invalid store startup submission");
		if (phase_ == Phase::Inventory)
			inventory_->submitted();
		else if (phase_ == Phase::CreateMetadata) {
			metadata_->submitted();
			data_->core->ledger.accepted(data_->metadata_permit);
		} else if (phase_ == Phase::Read)
			data_->reader.submitted();
		submitted_ = true;
	}
	bool complete(c::Result result) noexcept override {
		if (!submitted_ || result.token != request().token)
			return false;
		submitted_ = false;
		try {
			if (phase_ == Phase::Inventory) {
				const auto &old = inventory_->request();
				bool observe = old.kind == c::Kind::InspectEntry && !result.error && result.inspection;
				std::optional<c::EntryInspection> entry = result.inspection;
				if (observe) {
					data_->name_size = old.source.size();
					std::copy(old.source.begin(), old.source.end(), data_->name.begin());
				}
				inventory_->complete(std::move(result));
				if (observe && !inventory_->done())
					observe_entry(*entry);
				if (phase_ == Phase::Inventory && inventory_->done())
					finish_inventory();
			} else if (phase_ == Phase::CreateMetadata) {
				data_->core->ledger.completed(data_->metadata_permit);
				metadata_->complete(std::move(result));
				if (metadata_->done()) {
					if (metadata_->error())
						std::rethrow_exception(metadata_->error());
					data_->core->ledger.settle(data_->metadata_permit, {storage_identity_bytes, 1, 0});
					data_->metadata_permit = {};
					ready();
				}
			} else if (phase_ == Phase::Read) {
				data_->reader.complete(std::move(result));
				if (data_->reader.done()) {
					try {
						decode_record();
					} catch (...) {
						remember(std::current_exception());
					}
					prepare(data_->failure ? Phase::Close : Phase::Sync,
							data_->failure ? c::Kind::CloseFile : c::Kind::Sync);
				} else
					prepare_reader();
			} else {
				const auto phase = phase_;
				if (phase == Phase::Open && !result.error)
					data_->scratch_open = true;
				if (phase == Phase::Close)
					data_->scratch_open = false;
				if (result.error)
					remember(result.error);
				if (phase == Phase::Close) {
					if (data_->failure)
						drain_inventory();
					else {
						phase_ = Phase::Inventory;
						if (cancelled_)
							inventory_->cancel();
						if (inventory_->done())
							finish_inventory();
					}
				} else if (data_->failure) {
					if (data_->scratch_open)
						prepare(Phase::Close, c::Kind::CloseFile);
					else
						drain_inventory();
				} else if (phase == Phase::Open && !record_length_) {
					prepare(Phase::ObservedHeld, c::Kind::InspectFile);
				} else if (phase == Phase::ObservedHeld || phase == Phase::ObservedNamed) {
					if (!result.inspection || !safe(*result.inspection, *data_->core) ||
						result.inspection->footprint.logical_bytes !=
							data_->observed.footprint.logical_bytes ||
						result.inspection->device != data_->observed.device ||
						result.inspection->inode != data_->observed.inode)
						throw std::runtime_error("selected file changed during validation");
					if (phase == Phase::ObservedHeld)
						prepare(Phase::ObservedNamed, c::Kind::InspectEntry);
					else
						prepare(Phase::Sync, c::Kind::Sync);
				} else if (phase == Phase::Sync) {
					prepare(Phase::Close, c::Kind::CloseFile);
				} else if (phase == Phase::Open) {
					data_->reader.start(data_->observed_name(), record_length_, data_->core->owner,
										data_->core->session->directory_identity());
					prepare_reader();
				} else if (phase == Phase::Barrier)
					ready();
			}
		} catch (...) {
			remember(std::current_exception());
			try {
				if (data_->scratch_open)
					prepare(Phase::Close, c::Kind::CloseFile);
				else
					drain_inventory();
			} catch (...) {
				terminal_failure();
			}
		}
		return true;
	}
	bool done() const noexcept override { return phase_ == Phase::Done; }
	bool in_flight() const noexcept override { return submitted_; }
	c::IO &io() const noexcept override { return *data_->session->backend(); }
	void cancel() noexcept {
		cancelled_ = true;
		if (phase_ == Phase::Inventory) {
			inventory_->cancel();
			if (inventory_->done()) {
				try {
					finish_inventory();
				} catch (...) {
					remember(std::current_exception());
					terminal_failure();
				}
			}
		}
		// A started metadata preparation must finish its durable ownership record.
	}

  private:
	enum class Phase {
		Inventory,
		Open,
		Read,
		ObservedHeld,
		ObservedNamed,
		Sync,
		Close,
		CreateMetadata,
		Barrier,
		Done
	};
	void remember(std::exception_ptr failure) noexcept {
		if (!data_->failure)
			data_->failure = failure;
	}
	void terminal_failure() noexcept {
		data_->core->fence(data_->failure);
		phase_ = Phase::Done;
	}
	void drain_inventory() {
		inventory_->cancel();
		if (inventory_->done())
			terminal_failure();
		else
			phase_ = Phase::Inventory;
	}
	void prepare(Phase phase, c::Kind kind) {
		phase_ = phase;
		request_ = {};
		request_.token = token_;
		++request_.token.step;
		token_ = request_.token;
		request_.kind = kind;
		if (kind == c::Kind::OpenInto || kind == c::Kind::CloseFile || kind == c::Kind::InspectFile ||
			kind == c::Kind::Sync)
			request_.file = std::shared_ptr<c::File>(data_, &data_->core->controls->scratch_file());
		if (kind == c::Kind::OpenInto || kind == c::Kind::InspectEntry)
			request_.source = data_->observed_name();
	}
	void prepare_reader() {
		phase_ = Phase::Read;
		++token_.step;
		request_ = data_->reader.prepare(
			token_, std::shared_ptr<c::File>(data_, &data_->core->controls->scratch_file()));
	}
	void observe_entry(const c::EntryInspection &entry) {
		auto &data = *data_;
		auto &core = *data.core;
		const auto name = data.observed_name();
		data.expected_segment.reset();
		if (name == "owner.lock") {
			if (!safe(entry, core) || entry.footprint.logical_bytes != 0)
				throw std::runtime_error("unsafe stable owner lock");
			data.owner_seen = true;
			return;
		}
		if (core.options.mode == StoreOpenMode::FreshEmpty)
			throw std::runtime_error("fresh storage directory contains existing entries");
		bool read = false;
		if (name == "storage.meta") {
			data.metadata_seen = true;
			record_length_ = storage_identity_bytes;
			read = true;
		} else if (name == "selected") {
			data.selected_seen = true;
			record_length_ = descriptor_bytes;
			read = true;
		} else if (const auto parsed = detail::parse_generation_name(name);
				   parsed && parsed->kind == detail::GenerationFile::Image && core.verified &&
				   parsed->generation == core.verified->generation) {
			if (!safe(entry, core) || entry.footprint.logical_bytes != core.verified->length)
				throw std::runtime_error("selected image differs from verified recovery");
			data.selected_image_seen = true;
			record_length_ = 0;
			data.observed = entry;
			prepare(Phase::Open, c::Kind::OpenInto);
			return;
		} else if (const auto parsed = detail::parse_generation_name(name);
				   parsed && parsed->kind == detail::GenerationFile::Lease) {
			++data.anchors;
			if (!safe(entry, core) || entry.footprint.logical_bytes != 0)
				data.upgrade_blocked = true;
			if (core.verified && parsed->generation == core.verified->generation) {
				data.selected_lease_seen = true;
				record_length_ = 0;
				read = true;
				if (entry.footprint.logical_bytes != 0) {
					record_length_ = generation_ownership_bytes;
					read = true;
				} else if (!safe(entry, core))
					throw std::runtime_error("unsafe selected legacy lease");
			}
		}
		if (!read && core.verified_paged) {
			if (const auto named = detail::parse_segment_name(name)) {
				const auto registry = core.verified_paged->registry();
				const auto index = detail::find_segment(registry->inputs(), named->segment);
				if (index != registry->inputs().size()) {
					const auto &mapping = registry->mapping(index);
					const auto ownership = mapping->ownership();
					if (!safe(entry, core))
						throw std::runtime_error("unsafe selected segment dependency");
					if (named->kind == detail::SegmentFile::Owner) {
						++data.segment_owners_seen;
						data.expected_segment = ownership;
						record_length_ = segment_ownership_bytes;
						read = true;
					} else {
						const auto identity = mapping->identity();
						if (entry.footprint.logical_bytes != ownership.length ||
							entry.device != identity.device || entry.inode != identity.inode)
							throw std::runtime_error("selected segment differs from verified recovery");
						++data.segment_packs_seen;
						record_length_ = 0;
						data.observed = entry;
						prepare(Phase::Open, c::Kind::OpenInto);
						return;
					}
				}
			}
		}
		if (read) {
			data.observed = entry;
			if (!safe(entry, core) || entry.footprint.logical_bytes != record_length_)
				throw std::runtime_error("unsafe managed storage record");
			prepare(Phase::Open, c::Kind::OpenInto);
		}
	}
	void decode_record() {
		if (data_->reader.status() != RecordReadStatus::Ready)
			throw std::runtime_error("managed storage record verification failed");
		const auto name = data_->observed_name();
		if (name == "storage.meta")
			data_->stored_identity = decode_storage_identity(data_->reader.bytes());
		else if (name == "selected")
			data_->selected = decode_descriptor(data_->reader.bytes());
		else if (data_->expected_segment) {
			if (decode_segment_ownership(data_->reader.bytes()) != *data_->expected_segment)
				throw std::runtime_error("selected segment ownership changed after recovery");
		} else {
			data_->selected_ownership = decode_generation_ownership(data_->reader.bytes());
			data_->selected_owned = true;
		}
	}
	void finish_inventory() {
		auto &data = *data_;
		auto &core = *data.core;
		if (data.failure) {
			terminal_failure();
			return;
		}
		if (inventory_->status() != InventoryStatus::Ready) {
			if (auto error = inventory_->error())
				std::rethrow_exception(error);
			core.status = StoreStatus::Closing;
			phase_ = Phase::Done;
			return;
		}
		if (!data.owner_seen)
			throw std::runtime_error("stable owner lock disappeared");
		const auto usage = inventory_->report().usage;
		core.ledger.initialize(usage);
		if (data.selected != core.verified ||
			(data.selected && (!data.selected_lease_seen || !data.selected_image_seen)))
			throw std::runtime_error("selected recovery differs from retained census evidence");
		if (core.verified_paged &&
			(data.segment_owners_seen != core.verified_paged->registry()->inputs().size() ||
			 data.segment_packs_seen != core.verified_paged->registry()->inputs().size()))
			throw std::runtime_error("selected segment dependencies missing from census");
		if (data.stored_identity) {
			core.identity = *data.stored_identity;
			if (data.selected && data.selected_owned &&
				data.selected_ownership != GenerationOwnership{core.identity, data.selected->generation,
															   data.selected->length, data.selected->checksum,
															   core.verified_paged ? 2u : 1u})
				throw std::runtime_error("selected generation ownership differs from storage identity");
			core.bootstrap->usage = {};
			prepare(Phase::Barrier, c::Kind::DirectorySync);
			return;
		}
		if (core.options.mode == StoreOpenMode::ExistingManaged)
			throw std::runtime_error("managed storage identity missing");
		if (core.options.mode == StoreOpenMode::FreshEmpty && usage != DiskUsage{0, 1, 0})
			throw std::runtime_error("fresh storage census is not empty");
		if (core.options.mode == StoreOpenMode::UpgradeLegacy &&
			(data.upgrade_blocked || (!data.selected && !data.anchors && usage != DiskUsage{0, 1, 0})))
			throw std::runtime_error("legacy storage identity creation lacks trustworthy evidence");
		if (cancelled_ || core.closing) {
			core.bootstrap->usage = {};
			core.status = StoreStatus::Closing;
			phase_ = Phase::Done;
			return;
		}
		data.metadata_permit = core.ledger.reserve({storage_identity_bytes, 1, 0});
		core.bootstrap->usage = {};
		phase_ = Phase::CreateMetadata;
	}
	void ready() noexcept {
		data_->core->selection = {data_->selected ? SelectionKnowledge::Known : SelectionKnowledge::Absent,
								  data_->selected};
		data_->core->status = data_->core->closing ? StoreStatus::Closing : StoreStatus::Ready;
		phase_ = Phase::Done;
	}
	std::shared_ptr<StartupData> data_;
	std::shared_ptr<InventoryOperation> inventory_;
	std::shared_ptr<c::ImagePreparation> metadata_;
	c::Token token_;
	c::Request request_;
	Phase phase_ = Phase::Inventory;
	std::size_t record_length_ = 0;
	bool submitted_ = false, cancelled_ = false;
};
} // namespace
namespace {
struct PublicationData {
	PublicationData(std::shared_ptr<detail::StoreCore> owner, const ImageBuilder &builder)
		: PublicationData(std::move(owner), builder.descriptor(), builder.bytes(), 1, {}) {}
	PublicationData(std::shared_ptr<detail::StoreCore> owner, Descriptor target,
					std::shared_ptr<const distribution::OwnedBuffer> bytes, unsigned layout,
					distribution::RetirementHandle<detail::PagedPublicationArtifacts> artifacts)
		: core(std::move(owner)), session(core->session), image(std::move(bytes)), descriptor(target),
		  names{detail::GenerationName(descriptor.generation, detail::GenerationFile::Lease),
				detail::GenerationName(descriptor.generation, detail::GenerationFile::Image),
				detail::GenerationName(descriptor.generation, detail::GenerationFile::Descriptor)},
		  artifacts(std::move(artifacts)) {
		ownership = encode_generation_ownership(
			{core->identity, descriptor.generation, descriptor.length, descriptor.checksum, layout});
		record = encode_descriptor(descriptor);
	}
	std::shared_ptr<detail::StoreCore> core;
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<const distribution::OwnedBuffer> image;
	Descriptor descriptor;
	std::array<detail::GenerationName, 3> names;
	std::array<char, generation_ownership_bytes> ownership;
	std::array<char, descriptor_bytes> record;
	distribution::RetirementHandle<detail::PagedPublicationArtifacts> artifacts;
	DiskLedger::Permit permit;
	DiskPublicationStatus status = DiskPublicationStatus::Preparing;
	std::exception_ptr failure;
};

class RetiredPreparations final : public distribution::RetirementNode {
  public:
	explicit RetiredPreparations(c::AllocationContext context)
		: children(c::OwnedAllocator<std::shared_ptr<c::ImagePreparation>>(context)) {}
	std::vector<std::shared_ptr<c::ImagePreparation>, c::OwnedAllocator<std::shared_ptr<c::ImagePreparation>>>
		children;

  protected:
	bool retire_step() noexcept override {
		for (unsigned i = 0; i < 16 && !children.empty(); ++i)
			children.pop_back();
		return children.empty();
	}
};
class PublicationOperation final : public c::Operation {
  public:
	explicit PublicationOperation(std::shared_ptr<PublicationData> data) : data_(std::move(data)) {
		auto &core = *data_->core;
		const auto &context = core.session->allocation_context();
		if (data_->artifacts) {
			paged_children_ = core.factory->make<RetiredPreparations>(context);
			paged_children_->children.reserve(data_->artifacts->artifacts().size());
			dependencies_ = std::allocate_shared<detail::PagedDependencyCheck>(
				c::OwnedAllocator<detail::PagedDependencyCheck>(context), *core.factory, core.session,
				data_->artifacts, core.owner);
		}
		for (std::size_t i = 0; i < child_count(); ++i) {
			std::string_view bytes;
			if (data_->artifacts)
				bytes = data_->artifacts->artifacts()[i].bytes();
			else if (i == 0)
				bytes = {data_->ownership.data(), data_->ownership.size()};
			else if (i == 1)
				bytes = data_->image->view();
			else
				bytes = {data_->record.data(), data_->record.size()};
			auto preparation = c::make_image_preparation(
				context, core.session->backend(), data_, core.session->next_operation(), child_name(i), data_,
				bytes, bytes.size(), core.session->backend()->make_closed_file());
			if (paged_children_)
				paged_children_->children.push_back(std::move(preparation));
			else
				children_[i] = std::move(preparation);
		}
		token_ = core.session->next_operation();
		if (dependencies_ && !dependencies_->done())
			phase_ = Phase::Dependencies;
		else
			prepare(c::Kind::InspectEntry);
	}
	const c::Request &request() const override {
		if (phase_ == Phase::Dependencies)
			return dependencies_->request();
		return phase_ == Phase::Prepare ? child(index_)->request() : request_;
	}
	void submitted() override {
		if (done() || submitted_)
			throw std::logic_error("invalid managed publication submission");
		if (phase_ == Phase::Dependencies)
			dependencies_->submitted();
		if (phase_ == Phase::Prepare)
			child(index_)->submitted();
		if (phase_ != Phase::Preflight && phase_ != Phase::Dependencies) {
			data_->core->ledger.accepted(data_->permit);
			started_ = true;
			if (phase_ == Phase::Prepare)
				child_started_ = true;
			if (phase_ == Phase::Replace) {
				// An accepted Replace can change the name even if its result fails.
				data_->core->selection = {SelectionKnowledge::Uncertain, {}};
			}
		}
		submitted_ = true;
	}
	bool complete(c::Result result) noexcept override {
		if (!submitted_ || result.token != request().token)
			return false;
		submitted_ = false;
		try {
			if (phase_ == Phase::Dependencies) {
				dependencies_->complete(std::move(result));
				if (!dependencies_->done())
					return true;
				if (dependencies_->error())
					std::rethrow_exception(dependencies_->error());
				if (cancelled_ || data_->core->closing)
					cancel_healthy();
				else {
					phase_ = Phase::Preflight;
					prepare(c::Kind::InspectEntry);
				}
				return true;
			}
			if (phase_ != Phase::Preflight && phase_ != Phase::Dependencies)
				data_->core->ledger.completed(data_->permit);
			if (phase_ == Phase::Prepare) {
				child(index_)->complete(std::move(result));
				if (!child(index_)->done())
					return true;
				if (child(index_)->error())
					std::rethrow_exception(child(index_)->error());
				if (index_ + 1 == child_count())
					prepared_ = child(index_)->prepared();
				++sealed_;
				child_started_ = false;
				if (cancelled_ || data_->core->closing) {
					cancel_healthy();
					return true;
				}
				if (++index_ < child_count())
					return true;
				check_final_credit();
				phase_ = Phase::Replace;
				prepare(c::Kind::Replace);
				return true;
			}
			if (result.error)
				std::rethrow_exception(result.error);
			if (phase_ == Phase::Preflight) {
				if (result.inspection) {
					data_->failure =
						std::make_exception_ptr(std::invalid_argument("generation name already exists"));
					data_->core->ledger.cancel_unstarted(data_->permit);
					finish(DiskPublicationStatus::Rejected);
				} else if (cancelled_ || data_->core->closing)
					cancel_healthy();
				else if (++index_ < child_count())
					prepare(c::Kind::InspectEntry);
				else {
					index_ = 0;
					phase_ = Phase::Prepare;
				}
			} else if (phase_ == Phase::Replace) {
				phase_ = Phase::Barrier;
				prepare(c::Kind::DirectorySync);
			} else if (phase_ == Phase::Barrier) {
				// Durability lives here, independent of coordinator acknowledgement.
				data_->core->selection = {SelectionKnowledge::Known, data_->descriptor};
				data_->core->ledger.settle(data_->permit, peak(),
										   old_selected_ ? DiskUsage{descriptor_bytes, 1, 0} : DiskUsage{});
				if (data_->artifacts)
					data_->core->pending_paged_recovery = data_->artifacts;
				else
					data_->core->verified_paged.reset();
				finish(DiskPublicationStatus::Published);
			}
		} catch (...) {
			data_->failure = std::current_exception();
			data_->core->fence(data_->failure);
			finish(DiskPublicationStatus::Fenced);
		}
		return true;
	}
	bool done() const noexcept override { return phase_ == Phase::Done; }
	bool in_flight() const noexcept override { return submitted_; }
	c::IO &io() const noexcept override { return *data_->session->backend(); }
	void admit() {
		old_selected_ = data_->core->selection.knowledge == SelectionKnowledge::Known;
		data_->permit = data_->core->ledger.reserve(peak());
		data_->core->active_generation = data_->descriptor.generation;
	}
	void cancel() noexcept {
		cancelled_ = true;
		if (submitted_ || done() || phase_ == Phase::Barrier || child_started_)
			return;
		try {
			if (phase_ == Phase::Dependencies) {
				dependencies_->cancel();
				if (!dependencies_->done())
					return;
				if (dependencies_->error())
					std::rethrow_exception(dependencies_->error());
			}
			cancel_healthy();
		} catch (...) {
			data_->failure = std::current_exception();
			data_->core->fence(data_->failure);
			finish(DiskPublicationStatus::Fenced);
		}
	}
	DiskPublicationStatus status() const noexcept { return data_->status; }
	std::exception_ptr error() const noexcept { return data_->failure; }

  private:
	enum class Phase { Dependencies, Preflight, Prepare, Replace, Barrier, Done };
	std::size_t child_count() const {
		return data_->artifacts ? data_->artifacts->artifacts().size() : children_.size();
	}
	const std::shared_ptr<c::ImagePreparation> &child(std::size_t i) const {
		return paged_children_ ? paged_children_->children.at(i) : children_.at(i);
	}
	std::string_view child_name(std::size_t i) const {
		return data_->artifacts ? data_->artifacts->artifacts()[i].filename() : data_->names[i].view();
	}
	DiskUsage peak() const {
		return data_->artifacts ? data_->artifacts->usage()
								: DiskLedger::generation_peak(data_->descriptor.length);
	}
	void prepare(c::Kind kind) {
		request_ = {};
		request_.token = token_;
		++request_.token.step;
		token_ = request_.token;
		request_.kind = kind;
		if (kind == c::Kind::InspectEntry)
			request_.source = child_name(index_);
		if (kind == c::Kind::Replace) {
			request_.source = prepared_->name();
			request_.destination = "selected";
		}
	}
	void check_final_credit() const {
		const auto used = data_->core->ledger.stats().used;
		if (old_selected_ && (used.bytes < descriptor_bytes || !used.entries))
			throw std::logic_error("selected descriptor credit exceeds retained census");
	}
	void cancel_healthy() {
		// No child can subsequently create a file. Preserve exactly the sealed prefix.
		if (!paged_children_)
			for (std::size_t i = sealed_; i < children_.size(); ++i)
				children_[i]->cancel();
		if (!started_)
			data_->core->ledger.cancel_unstarted(data_->permit);
		else {
			DiskUsage observed;
			if (data_->artifacts)
				observed = data_->artifacts->prefix_usage(sealed_);
			else {
				if (sealed_ >= 1)
					observed = {generation_ownership_bytes, 1, 1};
				if (sealed_ >= 2) {
					observed.bytes += data_->descriptor.length;
					++observed.entries;
				}
				if (sealed_ >= 3) {
					observed.bytes += descriptor_bytes;
					++observed.entries;
				}
			}
			data_->core->ledger.settle(data_->permit, observed);
		}
		finish(DiskPublicationStatus::Cancelled);
	}
	void finish(DiskPublicationStatus status) noexcept {
		data_->status = status;
		if (status != DiskPublicationStatus::Fenced)
			data_->core->active_generation.reset();
		phase_ = Phase::Done;
	}
	std::shared_ptr<PublicationData> data_;
	std::array<std::shared_ptr<c::ImagePreparation>, 3> children_;
	distribution::RetirementHandle<RetiredPreparations> paged_children_;
	std::shared_ptr<detail::PagedDependencyCheck> dependencies_;
	std::optional<c::PreparedImage> prepared_;
	c::Request request_;
	c::Token token_;
	Phase phase_ = Phase::Preflight;
	std::size_t index_ = 0, sealed_ = 0;
	bool submitted_ = false, started_ = false, child_started_ = false, cancelled_ = false,
		 old_selected_ = false;
};
} // namespace
namespace {
struct ReclaimSlot {
	GenerationId generation{};
	GenerationOwnership proof;
	std::optional<std::pair<distribution::Cursor, distribution::Profile>> image_scope;
	std::array<std::optional<c::EntryInspection>, 3> entries;
	bool open = false, valid = false;
};
struct ReclaimData {
	explicit ReclaimData(std::shared_ptr<detail::StoreCore> owner)
		: core(std::move(owner)), session(core->session), controls(core->controls) {}
	std::shared_ptr<detail::StoreCore> core;
	std::shared_ptr<StorageSession> session;
	std::shared_ptr<CollectorControls> controls;
	std::array<ReclaimSlot, CollectorControls::batch_capacity> slots;
	RecordReader reader;
	std::array<char, 256> scan_name{};
	std::array<char, 64> name{};
	std::array<char, paged::root_bytes> image_prefix{};
	std::size_t prefix_length = 0, prefix_offset = 0;
	std::size_t scan_size = 0, name_size = 0;
	DiskUsage deleted;
	CollectorControls::Claim claim;
	std::exception_ptr failure;
	bool cursor_open = false, scratch_open = false;
};
// One startup-admitted operation/state and cursor for an entire forward pass.
// The coordinator may run publication at a paused batch boundary.
class ReclaimOperation final : public c::Operation {
  public:
	explicit ReclaimOperation(std::shared_ptr<ReclaimData> data)
		: data_(std::move(data)), token_(data_->session->next_operation()) {}
	const c::Request &request() const override { return request_; }
	void submitted() override {
		if (done() || paused() || submitted_)
			throw std::logic_error("invalid reclamation submission");
		if (phase_ == Phase::ReadLease || phase_ == Phase::ReadDescriptor)
			data_->reader.submitted();
		if (phase_ == Phase::RemoveImage || phase_ == Phase::RemoveDescriptor || phase_ == Phase::RemoveLease)
			destructive_ = true;
		submitted_ = true;
	}
	bool complete(c::Result result) noexcept override {
		if (!submitted_ || result.token != request_.token)
			return false;
		submitted_ = false;
		try {
			const auto phase = phase_;
			if (phase == Phase::Scan && !result.error)
				data_->cursor_open = true;
			if (phase == Phase::OpenLease && !result.error && result.count)
				slot().open = true;
			if ((phase == Phase::OpenDescriptor || phase == Phase::OpenImage) && !result.error)
				data_->scratch_open = true;
			if (phase == Phase::CloseScratch)
				data_->scratch_open = false;
			if (phase == Phase::CloseSlot || phase == Phase::CloseBatch)
				slot().open = false;
			if (phase == Phase::CloseCursor)
				data_->cursor_open = false;
			if ((cancelled_ || data_->core->closing) && !destructive_) {
				if (phase == Phase::ReadLease || phase == Phase::ReadDescriptor) {
					data_->reader.complete(std::move(result));
					if (data_->reader.status() == RecordReadStatus::Failed && data_->reader.error())
						std::rethrow_exception(data_->reader.error());
					data_->reader.cancel();
				} else if (result.error)
					std::rethrow_exception(result.error);
				cleanup();
				return true;
			}
			if (phase == Phase::ReadLease || phase == Phase::ReadDescriptor) {
				data_->reader.complete(std::move(result));
				if (data_->reader.status() == RecordReadStatus::Failed) {
					if (auto error = data_->reader.error())
						std::rethrow_exception(error);
					throw std::runtime_error("reclamation record IO failed");
				}
				if (!data_->reader.done()) {
					prepare_reader(phase);
					return true;
				}
				bool valid = data_->reader.status() == RecordReadStatus::Ready;
				if (valid) {
					try {
						if (phase == Phase::ReadLease) {
							slot().proof = decode_generation_ownership(data_->reader.bytes());
							valid = slot().proof.storage == data_->core->identity &&
									slot().proof.generation == slot().generation;
						} else {
							const auto bytes = data_->reader.bytes();
							if (bytes.size() == descriptor_bytes) {
								const auto descriptor = decode_descriptor(bytes);
								valid = descriptor.generation == slot().generation &&
										descriptor.length == slot().proof.image_length &&
										descriptor.checksum == slot().proof.image_checksum;
								if (slot().image_scope)
									valid = valid && descriptor.cursor == slot().image_scope->first &&
											descriptor.profile == slot().image_scope->second;
							} else
								validate_descriptor_prefix(bytes);
						}
					} catch (...) {
						valid = false;
					}
				}
				if (phase == Phase::ReadLease) {
					if (!valid || protected_generation(slot().generation))
						close_slot();
					else
						named(Phase::InspectImage, c::Kind::InspectEntry, 1);
				} else {
					slot().valid = valid;
					prepare(Phase::CloseScratch, c::Kind::CloseFile);
				}
				return true;
			}
			if (result.error)
				std::rethrow_exception(result.error);
			if (data_->failure || stopping_) {
				cleanup();
				return true;
			}
			if (phase == Phase::Scan)
				next_name();
			else if (phase == Phase::Next) {
				if (!result.count) {
					eof_ = true;
					start_batch();
				} else {
					if (result.count >= data_->scan_name.size())
						throw std::runtime_error("reclamation name overflow");
					data_->scan_size = result.count;
					const auto name = std::string_view(data_->scan_name.data(), data_->scan_size);
					if (name == "." || name == ".." || name.find('/') != name.npos ||
						name.find('\0') != name.npos)
						throw std::runtime_error("invalid reclamation basename");
					prepare(Phase::Inspect, c::Kind::InspectEntry);
					request_.source = name;
				}
			} else if (phase == Phase::Inspect) {
				++slice_;
				if (++inspected_ > data_->core->options.census_work_limit)
					throw std::length_error("reclamation work allowance exhausted");
				const auto parsed =
					detail::parse_generation_name({data_->scan_name.data(), data_->scan_size});
				bool duplicate = false;
				if (parsed)
					for (std::size_t i = 0; i < count_; ++i)
						duplicate |= data_->slots[i].generation == parsed->generation;
				if (parsed && parsed->kind == detail::GenerationFile::Lease && !duplicate &&
					!protected_generation(parsed->generation) && result.inspection &&
					safe(*result.inspection, *data_->core) &&
					result.inspection->footprint.logical_bytes == generation_ownership_bytes) {
					index_ = count_;
					slot() = {};
					slot().generation = parsed->generation;
					slot().entries[0] = result.inspection;
					named(Phase::OpenLease, c::Kind::OpenCandidateInto, 0);
				} else
					continue_scan();
			} else if (phase == Phase::OpenLease) {
				if (!result.count)
					continue_scan();
				else
					prepare(Phase::Lock, c::Kind::ExclusiveLease);
			} else if (phase == Phase::Lock) {
				if (!result.count)
					close_slot();
				else
					start_reader(Phase::ReadLease, 0, generation_ownership_bytes);
			} else if (phase == Phase::InspectImage || phase == Phase::InspectDescriptor) {
				const auto part = phase == Phase::InspectImage ? 1u : 2u;
				const auto maximum = part == 1 ? slot().proof.image_length : descriptor_bytes;
				if (result.inspection && (!safe(*result.inspection, *data_->core) ||
										  result.inspection->footprint.logical_bytes > maximum)) {
					close_slot();
					return true;
				}
				slot().entries[part] = result.inspection;
				if (part == 1) {
					if (result.inspection && result.inspection->footprint.logical_bytes) {
						data_->prefix_length = std::min<std::uint64_t>(
							slot().proof.layout_version == 2 ? paged::root_bytes : header_bytes,
							result.inspection->footprint.logical_bytes);
						data_->prefix_offset = 0;
						slot().valid = true;
						reading_image_ = true;
						named(Phase::OpenImage, c::Kind::OpenInto, 1);
					} else
						named(Phase::InspectDescriptor, c::Kind::InspectEntry, 2);
				} else if (result.inspection && result.inspection->footprint.logical_bytes)
					named(Phase::OpenDescriptor, c::Kind::OpenInto, 2);
				else {
					slot().valid = true;
					accept_slot();
				}
			} else if (phase == Phase::OpenImage) {
				prepare(Phase::ImageHeld, c::Kind::InspectFile);
			} else if (phase == Phase::ImageHeld || phase == Phase::ImageNamed) {
				if (!result.inspection || !same_entry(*result.inspection, *slot().entries[1])) {
					slot().valid = false;
					prepare(Phase::CloseScratch, c::Kind::CloseFile);
				} else if (phase == Phase::ImageHeld)
					prepare_image_read();
				else
					prepare(Phase::CloseScratch, c::Kind::CloseFile);
			} else if (phase == Phase::ReadImage) {
				if (!result.count || result.count > data_->prefix_length - data_->prefix_offset)
					throw std::runtime_error("invalid image prefix read");
				data_->prefix_offset += result.count;
				if (data_->prefix_offset < data_->prefix_length)
					prepare_image_read();
				else {
					try {
						validate_image_prefix();
					} catch (...) {
						slot().valid = false;
					}
					named(Phase::ImageNamed, c::Kind::InspectEntry, 1);
				}
			} else if (phase == Phase::OpenDescriptor) {
				start_reader(Phase::ReadDescriptor, 2, slot().entries[2]->footprint.logical_bytes);
			} else if (phase == Phase::CloseScratch) {
				if (reading_image_ && slot().valid) {
					reading_image_ = false;
					named(Phase::InspectDescriptor, c::Kind::InspectEntry, 2);
				} else if (slot().valid)
					accept_slot();
				else
					close_slot();
			} else if (phase == Phase::CloseSlot) {
				slot() = {};
				continue_scan();
			} else if (phase == Phase::SyncLease) {
				if (++index_ < count_)
					prepare(Phase::SyncLease, c::Kind::Sync);
				else {
					index_ = 0;
					payload_part_ = 0;
					named(Phase::Recheck, c::Kind::InspectEntry, payload_part_);
				}
			} else if (phase == Phase::Recheck) {
				const auto &expected = slot().entries[payload_part_];
				if (bool(result.inspection) != bool(expected) ||
					(expected && !same_entry(*result.inspection, *expected))) {
					cleanup();
					return true;
				}
				if (++payload_part_ == 3) {
					payload_part_ = 0;
					++index_;
				}
				if (index_ < count_)
					named(Phase::Recheck, c::Kind::InspectEntry, payload_part_);
				else {
					index_ = 0;
					payload_part_ = 1;
					remove_payload();
				}
			} else if (phase == Phase::RemoveImage || phase == Phase::RemoveDescriptor ||
					   phase == Phase::RemoveLease) {
				const auto part = phase == Phase::RemoveImage		 ? 1u
								  : phase == Phase::RemoveDescriptor ? 2u
																	 : 0u;
				data_->deleted.bytes += slot().entries[part]->footprint.logical_bytes;
				++data_->deleted.entries;
				if (!part)
					++data_->deleted.generations;
				if (phase == Phase::RemoveImage) {
					payload_part_ = 2;
					remove_payload();
				} else if (phase == Phase::RemoveDescriptor) {
					++index_;
					payload_part_ = 1;
					remove_payload();
				} else if (++index_ < count_)
					named(Phase::RemoveLease, c::Kind::Remove, 0);
				else
					prepare(Phase::LeaseBarrier, c::Kind::DirectorySync);
			} else if (phase == Phase::PayloadBarrier) {
				index_ = 0;
				named(Phase::RemoveLease, c::Kind::Remove, 0);
			} else if (phase == Phase::LeaseBarrier) {
				barriers_done_ = true;
				index_ = 0;
				prepare(Phase::CloseBatch, c::Kind::CloseFile);
			} else if (phase == Phase::CloseBatch) {
				if (++index_ < count_)
					prepare(Phase::CloseBatch, c::Kind::CloseFile);
				else {
					if (!barriers_done_)
						throw std::logic_error("reclamation credit lacks namespace barriers");
					data_->core->ledger.reclaimed(data_->deleted);
					data_->deleted = {};
					data_->slots = {};
					count_ = index_ = 0;
					destructive_ = barriers_done_ = false;
					boundary();
				}
			} else if (phase == Phase::CloseCursor)
				finish();
		} catch (...) {
			if (!data_->failure)
				data_->failure = std::current_exception();
			data_->core->fence(data_->failure);
			try {
				cleanup();
			} catch (...) {
				phase_ = Phase::Done;
			}
		}
		return true;
	}
	bool done() const noexcept override { return phase_ == Phase::Done; }
	bool in_flight() const noexcept override { return submitted_; }
	c::IO &io() const noexcept override { return *data_->session->backend(); }
	bool paused() const noexcept { return phase_ == Phase::Paused; }
	bool active_batch() const noexcept {
		return count_ || (phase_ != Phase::Done && phase_ != Phase::Paused);
	}
	void start() {
		if (!done() || submitted_)
			throw std::logic_error("reclamation pass already active");
		const auto bound = data_->core->options.census_work_limit * 1024 + 4096;
		if (token_.step > std::numeric_limits<std::uint64_t>::max() - bound)
			throw std::overflow_error("reclamation cleanup identities exhausted");
		data_->claim = data_->controls->claim(CollectorUse::Reclaim);
		data_->slots = {};
		data_->deleted = {};
		data_->failure = {};
		count_ = index_ = slice_ = inspected_ = 0;
		eof_ = cancelled_ = destructive_ = barriers_done_ = stopping_ = reading_image_ = false;
		prepare(Phase::Scan, c::Kind::ScanInto);
	}
	void resume() {
		if (paused()) {
			slice_ = 0;
			next_name();
		}
	}
	void cancel() noexcept {
		cancelled_ = true;
		if (!submitted_ && !done() && !destructive_) {
			data_->reader.cancel();
			try {
				cleanup();
			} catch (...) {
				data_->core->fence(std::current_exception());
			}
		}
	}

  private:
	enum class Phase {
		Done,
		Scan,
		Next,
		Inspect,
		OpenLease,
		Lock,
		ReadLease,
		InspectImage,
		OpenImage,
		ImageHeld,
		ReadImage,
		ImageNamed,
		InspectDescriptor,
		OpenDescriptor,
		ReadDescriptor,
		CloseScratch,
		CloseSlot,
		SyncLease,
		Recheck,
		RemoveImage,
		RemoveDescriptor,
		PayloadBarrier,
		RemoveLease,
		LeaseBarrier,
		CloseBatch,
		CloseCursor,
		Paused
	};
	ReclaimSlot &slot() noexcept { return data_->slots[index_]; }
	bool same_entry(const c::EntryInspection &current, const c::EntryInspection &expected) const noexcept {
		return safe(current, *data_->core) && current.device == expected.device &&
			   current.inode == expected.inode && current.permissions == expected.permissions &&
			   current.footprint.logical_bytes == expected.footprint.logical_bytes;
	}
	void prepare_image_read() {
		prepare(Phase::ReadImage, c::Kind::Read);
		request_.offset = data_->prefix_offset;
		request_.destination_bytes =
			std::span<char>(data_->image_prefix).first(data_->prefix_length).subspan(data_->prefix_offset);
	}
	static bool matches_prefix(std::span<const char> bytes, std::size_t offset,
							   std::span<const char> expected) noexcept {
		if (bytes.size() <= offset)
			return true;
		const auto available = bytes.subspan(offset, std::min(expected.size(), bytes.size() - offset));
		return std::equal(available.begin(), available.end(), expected.begin());
	}
	static bool matches_length(std::span<const char> bytes, std::size_t offset, std::uint64_t length) {
		std::array<char, 8> expected{};
		write_integer(expected, 0, length, 8);
		return matches_prefix(bytes, offset, expected);
	}
	static bool supported_profile(std::span<const char> bytes, std::size_t offset) noexcept {
		constexpr std::array<char, 8> profile{0, 0, 0, 1, 0, 0, 0, 0};
		constexpr std::array<char, 7> capability_prefix{};
		return matches_prefix(bytes, offset, profile) &&
			   matches_prefix(bytes, offset + 8, capability_prefix) &&
			   (bytes.size() < offset + 16 || (static_cast<unsigned char>(bytes[offset + 15]) & ~1u) == 0);
	}
	static bool valid_identity(std::span<const char> bytes, std::size_t offset, std::size_t length) noexcept {
		return bytes.size() < offset + length || std::any_of(
													 bytes.begin() + offset, bytes.begin() + offset + length,
													 [](char value) { return value != 0; });
	}
	void validate_image_prefix() {
		const auto bytes = std::span<const char>(data_->image_prefix).first(data_->prefix_length);
		if (slot().proof.layout_version == 2) {
			slot().image_scope = detail::validate_paged_root_prefix(bytes);
			if (bytes.size() == paged::root_bytes) {
				if (paged::physical_commitment(bytes) != slot().proof.image_checksum)
					throw std::runtime_error("paged root checksum differs from ownership");
				const auto root = paged::decode_root(bytes, detail::paged_format_limits());
				slot().image_scope = std::pair(root.cursor, root.profile);
			}
			return;
		}
		constexpr std::string_view magic = "DTIMAGE1";
		const auto prefix = std::min(bytes.size(), magic.size());
		if (!std::equal(bytes.begin(), bytes.begin() + prefix, magic.begin()))
			throw std::runtime_error("unsupported image magic");
		constexpr std::array<char, 8> fields{0, 0, 0, 1, 0, 0, 0, static_cast<char>(header_bytes)};
		if (bytes.size() > 8 &&
			!std::equal(bytes.begin() + 8, bytes.begin() + std::min<std::size_t>(16, bytes.size()),
						fields.begin()))
			throw std::runtime_error("unsupported image header version or extent");
		if (bytes.size() == header_bytes) {
			const auto header = decode_header(bytes, slot().proof.image_length);
			if (header.layout.length != slot().proof.image_length)
				throw std::runtime_error("image header extent conflicts with ownership");
			slot().image_scope = std::pair(header.cursor, header.profile);
		}
		if (!matches_length(bytes, 16, slot().proof.image_length))
			throw std::runtime_error("partial image header extent conflicts with ownership");
		if (!matches_length(bytes, 40, header_bytes))
			throw std::runtime_error("partial image namespace offset conflicts with format");
		if (bytes.size() >= 40) {
			const auto layout = calculate_layout(read_integer(bytes, 24, 4), read_integer(bytes, 28, 4),
												 read_integer(bytes, 32, 8), 0, slot().proof.image_length);
			if (!matches_length(bytes, 48, layout.record_offset) ||
				!matches_length(bytes, 56, layout.override_offset) ||
				!matches_length(bytes, 64, layout.string_offset) ||
				!matches_length(bytes, 72, slot().proof.image_length - layout.string_offset))
				throw std::runtime_error("partial image regions conflict with ownership");
		}
		if (!valid_identity(bytes, 88, 16) || !valid_identity(bytes, 104, 16) ||
			!valid_identity(bytes, 120, 16) || !valid_identity(bytes, 136, 32))
			throw std::runtime_error("partial image has unconfigured identity");
		if (!supported_profile(bytes, 168))
			throw std::runtime_error("unsupported partial image profile or capabilities");
		if (bytes.size() > 184 &&
			!std::all_of(bytes.begin() + 184, bytes.end(), [](char value) { return value == 0; }))
			throw std::runtime_error("unsupported partial image reserved prefix");
	}
	void validate_descriptor_prefix(std::span<const char> bytes) {
		if (slot().image_scope) {
			const auto expected =
				encode_descriptor({slot().generation, slot().proof.image_length, slot().proof.image_checksum,
								   slot().image_scope->first, slot().image_scope->second});
			if (!std::equal(bytes.begin(), bytes.end(), expected.begin()))
				throw std::runtime_error("partial descriptor conflicts with image header");
			return;
		}
		constexpr std::array<char, 16> prefix{
			'D', 'T', 'S', 'E', 'L', 'E', 'C', 'T', 0, 0, 0, 1, 0, 0, 0, static_cast<char>(descriptor_bytes)};
		if (!std::equal(bytes.begin(), bytes.begin() + std::min(bytes.size(), prefix.size()), prefix.begin()))
			throw std::runtime_error("unsupported partial descriptor");
		if (bytes.size() > 16 &&
			!std::equal(bytes.begin() + 16, bytes.begin() + std::min<std::size_t>(32, bytes.size()),
						slot().generation.begin()))
			throw std::runtime_error("partial descriptor generation conflicts with ownership");
		if (!matches_length(bytes, 32, slot().proof.image_length))
			throw std::runtime_error("partial descriptor extent conflicts with ownership");
		if (bytes.size() > 40 &&
			!std::equal(bytes.begin() + 40, bytes.begin() + std::min<std::size_t>(72, bytes.size()),
						slot().proof.image_checksum.begin()))
			throw std::runtime_error("partial descriptor checksum conflicts with ownership");
		if (!supported_profile(bytes, 160))
			throw std::runtime_error("unsupported partial descriptor profile or capabilities");
		if (!valid_identity(bytes, 72, 16) || !valid_identity(bytes, 88, 16) ||
			!valid_identity(bytes, 104, 16) || !valid_identity(bytes, 120, 32))
			throw std::runtime_error("partial descriptor has unconfigured identity");
	}
	bool protected_generation(const GenerationId &generation) const noexcept {
		const auto &core = *data_->core;
		return core.selection.knowledge == SelectionKnowledge::Uncertain ||
			   (core.selection.descriptor && core.selection.descriptor->generation == generation) ||
			   (core.active_generation && *core.active_generation == generation);
	}
	void prepare(Phase phase, c::Kind kind) {
		phase_ = phase;
		request_ = {};
		if (token_.step == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("reclamation token exhausted");
		request_.token = token_;
		++request_.token.step;
		token_ = request_.token;
		request_.kind = kind;
		if (kind == c::Kind::ScanInto || kind == c::Kind::NextInto || kind == c::Kind::CloseCursor)
			request_.cursor = std::shared_ptr<c::DirectoryCursor>(data_, &data_->controls->cursor());
		if (kind == c::Kind::NextInto)
			request_.destination_bytes = data_->scan_name;
		if (kind == c::Kind::OpenCandidateInto || kind == c::Kind::ExclusiveLease ||
			phase == Phase::SyncLease || (kind == c::Kind::CloseFile && phase != Phase::CloseScratch))
			request_.file = std::shared_ptr<c::File>(data_, &data_->controls->lease(index_));
		if (phase == Phase::OpenDescriptor || phase == Phase::CloseScratch || phase == Phase::OpenImage ||
			phase == Phase::ImageHeld || phase == Phase::ReadImage)
			request_.file = std::shared_ptr<c::File>(data_, &data_->controls->scratch_file());
	}
	void set_name(std::size_t part) {
		const auto kind = part == 0	  ? detail::GenerationFile::Lease
						  : part == 1 ? detail::GenerationFile::Image
									  : detail::GenerationFile::Descriptor;
		const detail::GenerationName name(slot().generation, kind);
		data_->name_size = name.view().size();
		std::copy(name.view().begin(), name.view().end(), data_->name.begin());
	}
	void named(Phase phase, c::Kind kind, std::size_t part) {
		set_name(part);
		prepare(phase, kind);
		request_.source = {data_->name.data(), data_->name_size};
	}
	void prepare_reader(Phase phase) {
		phase_ = phase;
		++token_.step;
		auto *file =
			phase == Phase::ReadLease ? &data_->controls->lease(index_) : &data_->controls->scratch_file();
		request_ = data_->reader.prepare(token_, std::shared_ptr<c::File>(data_, file));
	}
	void start_reader(Phase phase, std::size_t part, std::size_t length) {
		set_name(part);
		data_->reader.start({data_->name.data(), data_->name_size}, length, data_->core->owner,
							data_->session->directory_identity());
		prepare_reader(phase);
	}
	void next_name() {
		if (cancelled_ || data_->core->closing) {
			cleanup();
			return;
		}
		prepare(Phase::Next, c::Kind::NextInto);
	}
	void continue_scan() {
		if (cancelled_ || data_->core->closing)
			cleanup();
		else if (slice_ >= data_->core->options.gc_scan_slice)
			start_batch();
		else
			next_name();
	}
	void close_slot() {
		reading_image_ = false;
		slot().valid = false;
		prepare(Phase::CloseSlot, c::Kind::CloseFile);
	}
	void accept_slot() {
		++count_;
		if (count_ == data_->slots.size())
			start_batch();
		else
			continue_scan();
	}
	void start_batch() {
		if (cancelled_ || data_->core->closing) {
			cleanup();
			return;
		}
		if (!count_) {
			boundary();
			return;
		}
		for (std::size_t i = 0; i < count_; ++i)
			if (protected_generation(data_->slots[i].generation)) {
				cleanup();
				return;
			}
		index_ = 0;
		payload_part_ = 0;
		prepare(Phase::SyncLease, c::Kind::Sync);
	}
	void remove_payload() {
		while (index_ < count_) {
			if (payload_part_ == 1) {
				if (slot().entries[1]) {
					named(Phase::RemoveImage, c::Kind::Remove, 1);
					return;
				}
				payload_part_ = 2;
			}
			if (slot().entries[2]) {
				named(Phase::RemoveDescriptor, c::Kind::Remove, 2);
				return;
			}
			++index_;
			payload_part_ = 1;
		}
		prepare(Phase::PayloadBarrier, c::Kind::DirectorySync);
	}
	void boundary() {
		if (eof_ || cancelled_ || data_->core->closing)
			cleanup();
		else
			phase_ = Phase::Paused;
	}
	void cleanup() {
		stopping_ = true;
		if (data_->scratch_open) {
			prepare(Phase::CloseScratch, c::Kind::CloseFile);
			return;
		}
		for (std::size_t i = 0; i < data_->slots.size(); ++i) {
			if (data_->slots[i].open) {
				index_ = i;
				prepare(Phase::CloseSlot, c::Kind::CloseFile);
				return;
			}
		}
		if (data_->cursor_open)
			prepare(Phase::CloseCursor, c::Kind::CloseCursor);
		else
			finish();
	}
	void finish() {
		if (data_->claim.serial)
			data_->controls->release(data_->claim);
		data_->claim = {};
		phase_ = Phase::Done;
	}
	std::shared_ptr<ReclaimData> data_;
	c::Token token_;
	c::Request request_;
	Phase phase_ = Phase::Done;
	std::size_t count_ = 0, index_ = 0, slice_ = 0, inspected_ = 0, payload_part_ = 1;
	bool submitted_ = false, eof_ = false, cancelled_ = false, destructive_ = false, barriers_done_ = false,
		 stopping_ = false, reading_image_ = false;
};
} // namespace
struct DiskStore::Impl {
	explicit Impl(std::shared_ptr<detail::StoreCore> value) : core(std::move(value)) {}
	std::shared_ptr<detail::StoreCore> core;
	std::shared_ptr<StartupOperation> startup;
	std::shared_ptr<PublicationOperation> publication;
	DiskPublicationStatus terminal_publication_status = DiskPublicationStatus::Idle;
	std::exception_ptr terminal_publication_error;
	std::shared_ptr<ReclaimOperation> reclaim;
};
DiskStore::DiskStore(FactoryTag, std::shared_ptr<detail::StoreCore> core)
	: impl_(distribution::make_storage_owner<Impl>(core->account, std::malloc, std::free, core)) {}
DiskStore::~DiskStore() = default;
StoreStatus DiskStore::status() const noexcept { return impl_->core->status; }
DiskStats DiskStore::stats() const noexcept {
	auto stats = impl_->core->ledger.stats();
	if (!stats.initialized)
		stats.reserved = impl_->core->bootstrap->usage;
	if (!stats.initialized)
		stats.peak = stats.reserved;
	return stats;
}
SelectionSnapshot DiskStore::selection() const noexcept { return impl_->core->selection; }
DirectoryIdentity DiskStore::directory_identity() const noexcept { return impl_->core->directory_identity; }
std::exception_ptr DiskStore::error() const noexcept { return impl_->core->failure; }
std::shared_ptr<c::Operation> DiskStore::operation() const {
	if (impl_->startup && !impl_->startup->done())
		return impl_->startup;
	if (impl_->publication && !impl_->publication->done())
		return impl_->publication;
	if (impl_->reclaim && !impl_->reclaim->done() && !impl_->reclaim->paused())
		return impl_->reclaim;
	return nullptr;
}
void DiskStore::advance() {
	if (impl_->publication && impl_->publication->done() && !impl_->publication->in_flight()) {
		impl_->terminal_publication_status = impl_->publication->status();
		impl_->terminal_publication_error = impl_->publication->error();
		impl_->publication.reset();
	}
	if (!impl_->core->closing && impl_->core->status == StoreStatus::Ready &&
		!impl_->core->pending_paged_recovery && (!impl_->publication || impl_->publication->done())) {
		if (impl_->reclaim->paused())
			impl_->reclaim->resume();
		else if (impl_->core->collect && impl_->reclaim->done()) {
			impl_->reclaim->start();
			impl_->core->collect = false;
		}
	}
	if (impl_->startup && impl_->startup->done() && impl_->core->closing &&
		(!impl_->publication || impl_->publication->done()) && (!impl_->reclaim || impl_->reclaim->done())) {
		impl_->reclaim.reset();
		impl_->publication.reset();
		impl_->startup.reset();
		impl_->core->drop_certificates();
		impl_->core->pending_paged_recovery.reset();
		impl_->core->verified_paged.reset();
		impl_->core->factory.reset();
		impl_->core->controls.reset();
		impl_->core->session.reset();
		impl_->core->status = StoreStatus::Closed;
	}
}
void DiskStore::request_collection() noexcept { impl_->core->collect = true; }
bool DiskStore::publication_ready() const noexcept {
	return impl_->core->status == StoreStatus::Ready && !impl_->core->closing &&
		   !impl_->core->pending_paged_recovery && (!impl_->publication || impl_->publication->done()) &&
		   (!impl_->reclaim || !impl_->reclaim->active_batch());
}
void DiskStore::begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base) {
	begin_impl(builder, expected_base, nullptr);
}
void DiskStore::begin(detail::PagedPlan &&plan) {
	auto &core = *impl_->core;
	try {
		if (!publication_ready())
			throw std::logic_error("managed store unavailable for publication");
		const auto descriptor = plan.descriptor();
		if (core.selection.knowledge != SelectionKnowledge::Known ||
			core.selection.descriptor != plan.base_descriptor() ||
			plan.directory_identity() != core.directory_identity ||
			plan.storage_identity() != core.identity ||
			!descriptor.cursor.same_identity(plan.base_descriptor().cursor) ||
			descriptor.cursor.revision <= plan.base_descriptor().cursor.revision ||
			descriptor.profile != plan.base_descriptor().profile || plan.bytes()->account() != core.account ||
			descriptor.length != paged::root_bytes ||
			descriptor.length > core.options.recovery_limits.bytes ||
			plan.root().bytes > core.options.recovery_limits.bytes - descriptor.length ||
			plan.root().records > core.options.recovery_limits.records ||
			plan.root().namespaces > core.options.recovery_limits.namespaces ||
			plan.counts().strings > core.options.recovery_limits.strings ||
			plan.counts().maximum_overrides > core.options.recovery_limits.overrides)
			throw std::invalid_argument("managed paged publication exact base or admission mismatch");
		auto artifacts = core.factory->make<detail::PagedPublicationArtifacts>(core.account);
		artifacts->populate(plan);
		if (artifacts->target_physical_bytes() > core.options.recovery_limits.bytes)
			throw std::invalid_argument("paged target packs exceed recovery byte limit");
		const auto &context = core.session->allocation_context();
		auto data = std::allocate_shared<PublicationData>(
			c::OwnedAllocator<PublicationData>(context), impl_->core, descriptor, plan.bytes(), 2, artifacts);
		auto operation = std::allocate_shared<PublicationOperation>(
			c::OwnedAllocator<PublicationOperation>(context), data);
		operation->admit();
		try {
			artifacts->attach(std::move(plan));
		} catch (...) {
			operation->cancel();
			throw;
		}
		impl_->publication.reset();
		impl_->terminal_publication_status = DiskPublicationStatus::Idle;
		impl_->terminal_publication_error = {};
		impl_->publication = std::move(operation);
	} catch (const distribution::StoragePressure &) {
		core.drop_certificates();
		throw;
	} catch (const std::bad_alloc &) {
		core.drop_certificates();
		throw;
	}
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
	const auto bytes = builder.bytes();
	const auto base =
		core.selection.descriptor ? std::optional(core.selection.descriptor->cursor) : std::nullopt;
	if (core.selection.knowledge == SelectionKnowledge::Uncertain || expected_base != base)
		throw std::invalid_argument("managed publication actual base mismatch");
	if (const auto pinned = builder.base_descriptor();
		pinned && (core.selection.descriptor != pinned ||
				   builder.base_directory_identity() != core.session->directory_identity()))
		throw std::invalid_argument("managed publication pinned generation mismatch");
	if (reset) {
		if (core.selection.knowledge != SelectionKnowledge::Known ||
			core.selection.descriptor != reset->expected_selected || builder.base_descriptor() ||
			!descriptor.cursor.same_identity(reset->configured) || descriptor.profile != reset->profile ||
			reset->expected_selected.cursor.same_identity(descriptor.cursor))
			throw std::invalid_argument("managed publication trusted identity reset mismatch");
	} else if (base &&
			   (!base->same_identity(descriptor.cursor) || descriptor.cursor.revision <= base->revision))
		throw std::invalid_argument("managed publication requires a forward scoped cursor");
	if (bytes->account() != core.account || bytes->size() != descriptor.length ||
		descriptor.length > core.options.recovery_limits.bytes)
		throw std::invalid_argument("managed image account or extent mismatch");
	impl_->publication.reset();
	const auto &context = core.session->allocation_context();
	auto data = std::allocate_shared<PublicationData>(c::OwnedAllocator<PublicationData>(context),
													  impl_->core, builder);
	auto operation =
		std::allocate_shared<PublicationOperation>(c::OwnedAllocator<PublicationOperation>(context), data);
	operation->admit();
	impl_->terminal_publication_status = DiskPublicationStatus::Idle;
	impl_->terminal_publication_error = {};
	impl_->publication = std::move(operation);
}
void DiskStore::cancel_publication() noexcept {
	if (impl_->publication)
		impl_->publication->cancel();
}
DiskPublicationStatus DiskStore::publication_status() const noexcept {
	return impl_->publication ? impl_->publication->status() : impl_->terminal_publication_status;
}
std::exception_ptr DiskStore::publication_error() const noexcept {
	return impl_->publication ? impl_->publication->error() : impl_->terminal_publication_error;
}
const std::shared_ptr<distribution::StorageLedger> &DiskStore::storage_account() const noexcept {
	return impl_->core->account;
}
void DiskStore::close() noexcept {
	impl_->core->closing = true;
	if (impl_->core->status != StoreStatus::Closed)
		impl_->core->status = StoreStatus::Closing;
	if (impl_->core->session)
		impl_->core->session->close();
	if (impl_->startup)
		impl_->startup->cancel();
	if (impl_->publication)
		impl_->publication->cancel();
	if (impl_->reclaim)
		impl_->reclaim->cancel();
	advance();
}
bool DiskStore::drained() const noexcept { return status() == StoreStatus::Closed; }
std::shared_ptr<DiskStore> make_disk_store(distribution::RetirementDomain &domain,
										   const std::filesystem::path &directory,
										   std::shared_ptr<void> admission, DiskStoreOptions options) {
	if (!admission || !options.gc_scan_slice || !options.census_work_limit)
		throw std::invalid_argument("invalid managed store admission or work bounds");
	if (options.census_work_limit > (std::numeric_limits<std::uint64_t>::max() - 4096) / 1024)
		throw std::length_error("managed work exceeds retained cleanup identity space");
	if (options.mode == StoreOpenMode::FreshEmpty &&
		(options.limits.bytes < storage_identity_bytes || options.limits.entries < 2))
		throw DiskPressure("fresh root bootstrap exceeds disk quota");
	auto account = domain.storage_account();
	auto core = std::allocate_shared<detail::StoreCore>(
		distribution::StorageAllocator<detail::StoreCore>(account), account, options, domain.factory());
	core->bootstrap = std::allocate_shared<detail::BootstrapCommitment>(
		distribution::StorageAllocator<detail::BootstrapCommitment>(account));
	core->bootstrap->caller = std::move(admission);
	if (options.mode == StoreOpenMode::FreshEmpty)
		core->bootstrap->usage = {storage_identity_bytes, 2, 0};
	auto store = std::allocate_shared<DiskStore>(distribution::StorageAllocator<DiskStore>(account),
												 DiskStore::FactoryTag{}, core);
	core->session = make_owned_storage_session(
		domain, directory, core->bootstrap, options.mode == StoreOpenMode::FreshEmpty, options.artifact_mode);
	auto *posix = dynamic_cast<c::PosixIO *>(core->session->backend().get());
	struct stat info{};
	if (!posix || ::fstat(posix->native_directory_handle(), &info) != 0)
		throw std::system_error(errno, std::generic_category(), "managed store directory inspection");
	core->owner = info.st_uid;
	core->directory_identity = core->session->directory_identity();
	core->controls = make_collector_controls(core->session);
	const auto &context = core->session->allocation_context();
	auto data = std::allocate_shared<StartupData>(c::OwnedAllocator<StartupData>(context), core);
	core->identity = fresh_identity();
	data->metadata = encode_storage_identity(core->identity);
	auto metadata = c::make_image_preparation(context, core->session->backend(), data,
											  core->session->next_operation(), "storage.meta", data,
											  std::string_view(data->metadata.data(), data->metadata.size()),
											  64, core->session->backend()->make_closed_file());
	auto inventory = make_inventory_operation(core->controls, options.census_work_limit, core->owner);
	store->impl_->startup = std::allocate_shared<StartupOperation>(
		c::OwnedAllocator<StartupOperation>(context), data, inventory, metadata);
	auto reclaim = std::allocate_shared<ReclaimData>(c::OwnedAllocator<ReclaimData>(context), core);
	store->impl_->reclaim =
		std::allocate_shared<ReclaimOperation>(c::OwnedAllocator<ReclaimOperation>(context), reclaim);
	try {
		if (core->session->backend()->inspect_entry("selected")) {
			auto recovered = recover_generation(directory, options.recovery_limits);
			if (recovered.directory_identity() != core->session->directory_identity())
				throw std::runtime_error("recovered directory differs from stable session");
			core->retain_recovered(recovered);
		}
	} catch (...) {
		data->failure = std::current_exception();
		core->fence(data->failure);
		store->impl_->startup->cancel();
	}
	return store;
}
} // namespace detent::local
