#include "paged_plan.hh"
#include <algorithm>
#include <stdexcept>
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
PagedPlanState::PagedPlanState(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> base, GenerationId generation,
							   const d::ValidatedDelta &source)
	: base_(std::move(base)) {
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
	return base_->descriptor();
}
DirectoryIdentity PagedPlanState::directory_identity() const {
	descriptor();
	return base_->directory_identity();
}
StorageIdentity PagedPlanState::storage_identity() const {
	descriptor();
	return base_->storage_identity();
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
	return bytes_;
}
bool PagedPlanState::retire_step() noexcept {
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
