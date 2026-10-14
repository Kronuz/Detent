#include "paged_plan.hh"
#include <algorithm>
#include <stdexcept>
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> base, GenerationId generation,
							   const d::ValidatedDelta &source)
	: base_(std::move(base)), base_live_(d::StorageAllocator<SegmentReachability>(account)),
	  new_live_(d::StorageAllocator<SegmentReachability>(account)) {
	if (!base_ || source.kind() != d::UpdateKind::Progress || source.transactions().size() != 0 ||
		source.base() != base_->descriptor().cursor || !source.target().same_identity(source.base()) ||
		source.target().revision <= source.base().revision ||
		source.profile() != base_->descriptor().profile || generation == base_->descriptor().generation)
		throw std::invalid_argument("invalid exact-base paged progress");
	root_ = base_->root();
	root_.cursor = source.target();
	descriptor_ = {generation, p::root_bytes, {}, root_.cursor, root_.profile};
	encode_descriptor(descriptor_);
	encoded_ = p::encode_root(root_);
	counts_ = base_->counts();
	registry_ = base_->registry();
	bytes_ = d::make_owned_buffer(account, p::root_bytes);
}
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> base, GenerationId generation,
							   const d::ValidatedDelta &source, Forward)
	: base_(std::move(base)), base_live_(d::StorageAllocator<SegmentReachability>(account)),
	  new_live_(d::StorageAllocator<SegmentReachability>(account)) {
	if (!base_ || source.kind() != d::UpdateKind::Delta || source.base() != base_->descriptor().cursor ||
		!source.target().same_identity(source.base()) || source.target().revision <= source.base().revision ||
		source.profile() != base_->descriptor().profile || generation == base_->descriptor().generation)
		throw std::invalid_argument("invalid exact-base paged delta");
	registry_ = base_->registry();
	descriptor_ = {generation, p::root_bytes, {}, source.target(), source.profile()};
	encode_descriptor(descriptor_);
}
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> base, GenerationId generation,
							   PhysicalMaintenance)
	: base_(std::move(base)), base_live_(d::StorageAllocator<SegmentReachability>(account)),
	  new_live_(d::StorageAllocator<SegmentReachability>(account)) {
	if (!base_ || !base_->registry()->uses_account(account) || generation == base_->descriptor().generation)
		throw std::invalid_argument("compaction requires actual paged base and common admission");
	registry_ = base_->registry();
	descriptor_ = {generation, p::root_bytes, {}, base_->descriptor().cursor, base_->descriptor().profile};
	encode_descriptor(descriptor_);
}
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account, RecoveredGeneration base,
							   d::RetirementHandle<PagedImageOwner> replacement, StorageIdentity storage)
	: PagedPlanState(std::move(account), std::optional<RecoveredGeneration>(base), std::move(replacement),
					 storage, base.directory_identity(), PagedPlanIntent::Maintenance) {}
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account,
							   std::optional<RecoveredGeneration> base,
							   d::RetirementHandle<PagedImageOwner> replacement, StorageIdentity storage,
							   DirectoryIdentity directory, PagedPlanIntent intent)
	: construction_base_(std::move(base)), replacement_(std::move(replacement)),
	  replacement_storage_(storage), construction_directory_(directory), intent_(intent),
	  base_live_(d::StorageAllocator<SegmentReachability>(account)),
	  new_live_(d::StorageAllocator<SegmentReachability>(account)) {
	if (!replacement_ || replacement_->image().bytes()->account() != account)
		throw std::invalid_argument("snapshot image account mismatch");
	root_ = replacement_->image().root();
	descriptor_ = replacement_->image().descriptor();
	if ((intent == PagedPlanIntent::Fresh) != !construction_base_)
		throw std::invalid_argument("snapshot intent and actual base mismatch");
	if (construction_base_) {
		const auto &prior = construction_base_->descriptor();
		if (construction_base_->directory_identity() != directory ||
			descriptor_.generation == prior.generation)
			throw std::invalid_argument("snapshot physical base mismatch");
		const bool same = descriptor_.cursor.same_identity(prior.cursor);
		if ((intent == PagedPlanIntent::Forward &&
			 (!same || descriptor_.cursor.revision <= prior.cursor.revision ||
			  descriptor_.profile != prior.profile)) ||
			(intent == PagedPlanIntent::Maintenance &&
			 (descriptor_.cursor != prior.cursor || descriptor_.profile != prior.profile)) ||
			(intent == PagedPlanIntent::TrustedReset && same))
			throw std::invalid_argument("snapshot target intent mismatch");
	}
	new_live_.reserve(replacement_->image().segments().size());
}
PagedPlanIntent PagedPlanState::intent() const {
	descriptor();
	return maintenance_proof_ ? PagedPlanIntent::Maintenance : intent_;
}
std::optional<Descriptor> PagedPlanState::selected_base() const {
	descriptor();
	if (base_)
		return base_->descriptor();
	if (construction_base_)
		return construction_base_->descriptor();
	return {};
}
bool PagedPlanState::maintenance() const {
	descriptor();
	return bool(maintenance_proof_);
}
const Descriptor &PagedPlanState::descriptor() const {
	if (!sealed_)
		throw std::logic_error("unsealed paged publication plan");
	return descriptor_;
}
const p::Root &PagedPlanState::root() const {
	descriptor();
	return root_;
}
const Descriptor &PagedPlanState::base_descriptor() const {
	descriptor();
	if (base_)
		return base_->descriptor();
	if (construction_base_)
		return construction_base_->descriptor();
	throw std::logic_error("fresh paged plan has no selected base");
}
DirectoryIdentity PagedPlanState::directory_identity() const {
	descriptor();
	return base_ ? base_->directory_identity() : construction_directory_;
}
StorageIdentity PagedPlanState::storage_identity() const {
	descriptor();
	return base_ ? base_->storage_identity() : replacement_storage_;
}
const d::RetirementHandle<SegmentRegistry> &PagedPlanState::registry() const {
	descriptor();
	return registry_;
}
PagedStateCounts PagedPlanState::counts() const {
	descriptor();
	return counts_;
}
std::shared_ptr<const d::OwnedBuffer> PagedPlanState::bytes() const {
	descriptor();
	return image_ ? image_->bytes() : replacement_ ? replacement_->image().bytes() : bytes_;
}
std::span<const BuiltSegment> PagedPlanState::segments() const {
	descriptor();
	return image_		  ? image_->segments()
		   : replacement_ ? replacement_->image().segments()
						  : std::span<const BuiltSegment>{};
}
SegmentReachability PagedPlanState::base_reachability(std::size_t index) const {
	descriptor();
	return image_ || replacement_ ? base_live_.at(index) : registry_->reachability(index);
}
SegmentReachability PagedPlanState::new_reachability(std::size_t index) const {
	descriptor();
	return new_live_.at(index);
}
std::span<const std::uint32_t, 257> PagedPlanState::override_histogram() const {
	descriptor();
	return image_ || replacement_ ? std::span<const std::uint32_t, 257>(histogram_)
								  : registry_->override_histogram();
}
bool PagedPlanState::retire_step() noexcept {
	if (replacement_) {
		replacement_.reset();
		return false;
	}
	if (construction_base_) {
		construction_base_.reset();
		return false;
	}
	if (image_) {
		if (image_capacity_) {
			image_capacity_ -= std::min<std::size_t>(image_capacity_, 8192);
			return false;
		}
		ValidationBudget budget{0, 16};
		if (!image_->discard(budget))
			return false;
		image_.reset();
		return false;
	}
	if (bytes_) {
		bytes_.reset();
		return false;
	}
	if (registry_) {
		registry_.reset();
		return false;
	}
	if (base_) {
		base_.reset();
		return false;
	}
	return true;
}
PagedPlanBuilder::PagedPlanBuilder(const RecoveredGeneration &base,
								   std::shared_ptr<const d::ValidatedDelta> source, GenerationId generation,
								   d::RetirementDomain &domain) {
	if (!source || !base.paged_ || source->storage_account() != domain.storage_account())
		throw std::invalid_argument("paged plan requires recovered base and common admission");
	plan_ = domain.make<PagedPlanState>(source->storage_account(), base.paged_, generation, *source);
}
PagedPlanBuilder::~PagedPlanBuilder() = default;
PagedPlanBuilder::PagedPlanBuilder(PagedPlanBuilder &&) noexcept = default;
void PagedPlanBuilder::step(ValidationBudget &budget) {
	if (!plan_ || failed_)
		throw std::logic_error("paged plan construction unavailable");
	auto &s = *plan_;
	try {
		auto n = std::min(budget.bytes, s.encoded_.size() - s.copied_);
		if (n) {
			s.bytes_->append({s.encoded_.data() + s.copied_, n});
			s.copied_ += n;
			budget.bytes -= n;
		}
		if (s.copied_ != s.encoded_.size())
			return;
		n = std::min(budget.bytes, s.encoded_.size() - s.hashed_);
		if (n) {
			s.hash_.add(s.encoded_.data() + s.hashed_, n);
			s.hashed_ += n;
			budget.bytes -= n;
		}
		if (s.hashed_ != s.encoded_.size() || !budget.rows)
			return;
		if (!s.sealed_) {
			--budget.rows;
			s.bytes_->seal();
			std::array<unsigned char, 32> digest{};
			s.hash_.getHash(digest.data());
			std::copy(digest.begin(), digest.end(), s.descriptor_.checksum.begin());
			s.sealed_ = true;
		}
	} catch (...) {
		failed_ = true;
		throw;
	}
}
bool PagedPlanBuilder::done() const noexcept { return plan_ && !failed_ && plan_->sealed_; }
PagedPlan PagedPlanBuilder::take() {
	if (!done())
		throw std::logic_error("paged plan construction incomplete");
	return PagedPlan(std::move(plan_));
}
bool PagedPlanBuilder::discard(ValidationBudget &budget) {
	if (!budget.rows)
		return !plan_;
	--budget.rows;
	plan_.reset();
	return true;
}
} // namespace detent::local::detail
