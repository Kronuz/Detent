#include "paged_publication_artifacts.hh"
#include "names.hh"
#include "reader_capsule.hh"
#include "segment_names.hh"
#include <limits>
namespace detent::local::detail {
namespace d = distribution;
PagedPublicationArtifacts::PagedPublicationArtifacts(std::shared_ptr<d::StorageLedger> account)
	: artifacts_(Allocator(account)), reused_(d::StorageAllocator<ReusedDependency>(account)) {}
void PagedPublicationArtifacts::append(std::string_view name, std::span<const char> record,
									   std::shared_ptr<const d::OwnedBuffer> payload, bool generation) {
	if (name.size() > PublicationArtifact{}.name.size() || record.size() > descriptor_bytes)
		throw std::length_error("paged publication artifact exceeds fixed framing");
	const auto length = payload ? payload->size() : record.size();
	if (length > UINT64_MAX - usage_.bytes || usage_.entries == UINT64_MAX ||
		(generation && usage_.generations == UINT64_MAX))
		throw std::length_error("paged publication accounting overflow");
	PublicationArtifact row;
	std::copy(name.begin(), name.end(), row.name.begin());
	row.name_size = name.size();
	std::copy(record.begin(), record.end(), row.record.begin());
	row.record_size = record.size();
	row.payload = std::move(payload);
	row.prefix = {usage_.bytes + length, usage_.entries + 1, usage_.generations + generation};
	artifacts_.push_back(std::move(row));
	usage_ = artifacts_.back().prefix;
}
void PagedPublicationArtifacts::populate(const PagedPlan &plan, ReaderMetadataEncoding encoding) {
	if (populated_ || failed_ || !artifacts_.empty())
		throw std::logic_error("paged artifacts already populated or failed");
	try {
		if (artifacts_.get_allocator() != Allocator(plan.bytes()->account()))
			throw std::invalid_argument("paged artifact account mismatch");
		const auto segments = plan.segments();
		if (segments.size() > (std::numeric_limits<std::size_t>::max() - 4) / 2)
			throw std::length_error("paged publication artifact count exceeded");
		artifacts_.reserve(2 * segments.size() + 4);
		const auto registry = plan.registry();
		reused_.reserve(registry ? registry->inputs().size() : 0);
		for (std::size_t i = 0; registry && i < registry->inputs().size(); ++i) {
			if (plan.base_reachability(i).pages) {
				const auto &mapping = registry->mapping(i);
				if (mapping->ownership().length > UINT64_MAX - physical_bytes_)
					throw std::length_error("paged target physical size overflow");
				physical_bytes_ += mapping->ownership().length;
				reused_.push_back({mapping->ownership(), mapping->identity()});
			}
		}
		for (std::size_t i = 0; i < segments.size(); ++i) {
			const auto &segment = segments[i];
			if (!plan.new_reachability(i).pages || segment.bytes->account() != plan.bytes()->account() ||
				segment.bytes->size() != segment.ownership.length ||
				segment.ownership.storage != plan.storage_identity())
				throw std::invalid_argument("invalid new paged publication dependency");
			const SegmentNames names(segment.ownership.segment);
			if (segment.ownership.length > UINT64_MAX - physical_bytes_)
				throw std::length_error("paged target physical size overflow");
			physical_bytes_ += segment.ownership.length;
			append(names.owner_name(), encode_segment_ownership(segment.ownership), {});
			append(names.payload_name(), {}, segment.bytes);
		}
		if (reused_.size() + segments.size() != plan.root().segments)
			throw std::invalid_argument("paged publication dependency count mismatch");
		const auto descriptor = plan.descriptor();
		append(GenerationName(descriptor.generation, GenerationFile::Lease).view(),
			   encode_generation_ownership({plan.storage_identity(), descriptor.generation, descriptor.length,
											descriptor.checksum, 2}),
			   {}, true);
		append(GenerationName(descriptor.generation, GenerationFile::Image).view(), {}, plan.bytes());
		const ReaderMetadata metadata{plan.storage_identity(), descriptor.generation,
		                              descriptor.checksum, plan.counts(), physical_bytes_};
		auto metadata_buffer = d::make_owned_buffer(plan.bytes()->account(), reader_metadata_length(encoding));
		if (encoding == ReaderMetadataEncoding::Capsule) {
			const auto bytes = encode_reader_capsule({plan.storage_identity(), plan.root(), metadata});
			metadata_buffer->append({bytes.data(), bytes.size()});
		} else {
			const auto bytes = encode_reader_metadata(metadata);
			metadata_buffer->append({bytes.data(), bytes.size()});
		}
		append(GenerationName(descriptor.generation, GenerationFile::ReaderMetadata).view(), {},
		       std::move(metadata_buffer));
		append(GenerationName(descriptor.generation, GenerationFile::Descriptor).view(),
			   encode_descriptor(descriptor), {});
		descriptor_ = descriptor;
		base_descriptor_ = plan.selected_base();
		directory_ = plan.directory_identity();
		storage_ = plan.storage_identity();
		populated_ = true;
	} catch (...) {
		failed_ = true;
		throw;
	}
}
void PagedPublicationArtifacts::attach(PagedPlan &&plan) {
	if (!populated_ || plan_ || plan.descriptor() != descriptor_ ||
		plan.selected_base() != base_descriptor_ || plan.directory_identity() != directory_ ||
		plan.storage_identity() != storage_)
		throw std::invalid_argument("paged artifact plan binding mismatch");
	plan_.emplace(std::move(plan));
}
std::span<const PublicationArtifact> PagedPublicationArtifacts::artifacts() const {
	if (!populated_)
		throw std::logic_error("unpopulated paged publication artifacts");
	return artifacts_;
}
std::span<const ReusedDependency> PagedPublicationArtifacts::reused() const {
	artifacts();
	return reused_;
}
const PagedPlan &PagedPublicationArtifacts::plan() const {
	if (!plan_)
		throw std::logic_error("unattached paged artifact plan");
	return *plan_;
}
DiskUsage PagedPublicationArtifacts::usage() const {
	artifacts();
	return usage_;
}
std::uint64_t PagedPublicationArtifacts::target_physical_bytes() const {
	artifacts();
	return physical_bytes_;
}
DiskUsage PagedPublicationArtifacts::prefix_usage(std::size_t count) const {
	artifacts();
	if (count > artifacts_.size())
		throw std::out_of_range("paged artifact prefix exceeded");
	return count ? artifacts_[count - 1].prefix : DiskUsage{};
}
void PagedPublicationArtifacts::attach_recovery(d::RetirementHandle<MappedRecoveryAdmission> admission) {
	artifacts();
	if (!admission || recovery_ || plan_)
		throw std::logic_error("paged recovery admission attachment unavailable");
	admission->bind(descriptor_, directory_, storage_);
	recovery_ = std::move(admission);
}
d::RetirementHandle<MappedRecoveryAdmission> PagedPublicationArtifacts::recovery_admission() const {
	artifacts();
	if (!recovery_)
		throw std::logic_error("missing paged recovery admission");
	return recovery_;
}
bool PagedPublicationArtifacts::retire_step() noexcept {
	recovery_.reset();
	for (unsigned i = 0; i < 16 && !artifacts_.empty(); ++i)
		artifacts_.pop_back();
	if (!artifacts_.empty())
		return false;
	for (unsigned i = 0; i < 16 && !reused_.empty(); ++i)
		reused_.pop_back();
	if (!reused_.empty())
		return false;
	plan_.reset();
	return true;
}
} // namespace detent::local::detail
