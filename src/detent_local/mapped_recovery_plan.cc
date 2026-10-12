#include "mapped_recovery_admission.hh"
#include "paged_plan.hh"
namespace detent::local::detail {
namespace d = distribution;
void MappedRecoveryAdmission::populate(const PagedPlan &plan, d::RetirementHandle<LeasedPagedRoot> prior) {
	if (populated_ || failed_ || root_ || !rows_.empty())
		throw std::logic_error("mapped admission unavailable");
	try {
		target_ = plan.descriptor();
		base_ = plan.base_descriptor();
		directory_ = plan.directory_identity();
		storage_ = plan.storage_identity();
		if (plan.bytes()->account() != heap_)
			throw std::invalid_argument("mapped admission plan account mismatch");
		warm_ = prior && prior->descriptor() == base_ && prior->directory_identity() == directory_ &&
				prior->storage_identity() == storage_ && prior->registry()->uses_account(heap_);
		if (warm_)
			for (std::size_t i = 0; i < prior->registry()->inputs().size(); ++i)
				if (prior->registry()->mapping(i)->storage_account() != mappings_) {
					warm_ = false;
					break;
				}
		std::size_t count = plan.segments().size();
		if (!warm_)
			for (std::size_t i = 0; i < plan.registry()->inputs().size(); ++i)
				if (plan.base_reachability(i).pages)
					++count;
		if (count > paged::Limits{}.segments)
			throw std::length_error("mapped admission segment limit exceeded");
		rows_.reserve(count);
		root_ = factory_.make<GenerationMapping>(mappings_);
		root_->length = target_.length;
		root_->ticket = mappings_->admit(rounded_mapping_bytes(target_.length));
		bytes_ = root_->ticket.bytes();
		if (!warm_)
			for (std::size_t i = 0; i < plan.registry()->inputs().size(); ++i)
				if (plan.base_reachability(i).pages)
					append(plan.registry()->inputs()[i].ownership);
		for (const auto &segment : plan.segments())
			append(segment.ownership);
		directory_control_ = factory_.make<GenerationDirectory>();
		using Directory = d::RetirementHandle<GenerationDirectory>;
		directory_hook_ =
			std::allocate_shared<Directory>(d::StorageAllocator<Directory>(heap_), directory_control_);
		using Mapping = d::RetirementHandle<GenerationMapping>;
		root_hook_ = std::allocate_shared<Mapping>(d::StorageAllocator<Mapping>(heap_), root_);
		paged::Limits limits;
		limits.segments = plan.root().segments;
		const bool progress = warm_ && reuses_paged_state(prior->root(), plan.root());
		target_registry_ =
			progress ? prior->registry() : factory_.make<SegmentRegistry>(heap_, storage_, limits);
		if (warm_ && !progress) {
			limits.segments = plan.segments().size();
			fresh_registry_ = factory_.make<SegmentRegistry>(heap_, storage_, limits);
			using Registry = d::RetirementHandle<SegmentRegistry>;
			fresh_hook_ =
				std::allocate_shared<Registry>(d::StorageAllocator<Registry>(heap_), fresh_registry_);
		}
		std::shared_ptr<RecoveryFile> directory(directory_hook_, &directory_control_->file);
		capability_ = factory_.make<LeasedPagedRoot>(directory, root_hook_, target_registry_, target_,
													 directory_, storage_, plan.root());
		using Capability = d::RetirementHandle<LeasedPagedRoot>;
		capability_hook_ =
			std::allocate_shared<Capability>(d::StorageAllocator<Capability>(heap_), capability_);
		view_ = factory_.make<ReadView::Root>();
		populated_ = true;
	} catch (...) {
		failed_ = true;
		throw;
	}
}
} // namespace detent::local::detail
