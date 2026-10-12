#include "mapped_recovery_admission.hh"
#include <algorithm>
#include <cstring>
namespace detent::local::detail {
namespace d = distribution;
MappedRecoveryAdmission::MappedRecoveryAdmission(std::shared_ptr<d::StorageLedger> heap,
												 std::shared_ptr<d::StorageLedger> mappings,
												 d::RetirementFactory factory)
	: rows_(d::StorageAllocator<Row>(heap)), heap_(std::move(heap)), mappings_(std::move(mappings)),
	  factory_(std::move(factory)) {
	if (!heap_ || !mappings_ || !factory_.uses_account(heap_))
		throw std::invalid_argument("mapped admission requires exact ledgers");
}
void MappedRecoveryAdmission::append(SegmentOwnership ownership) {
	encode_segment_ownership(ownership);
	if (ownership.storage != storage_)
		throw std::invalid_argument("mapped admission storage mismatch");
	const auto found =
		std::lower_bound(rows_.begin(), rows_.end(), ownership.segment, [](const Row &row, const auto &id) {
			return std::memcmp(row.ownership.segment.data(), id.data(), id.size()) < 0;
		});
	if (found != rows_.end() && found->ownership.segment == ownership.segment)
		throw std::invalid_argument("duplicate staged mapping");
	auto mapping = factory_.make<SegmentMapping>(mappings_, ownership);
	const auto bytes = rounded_mapping_bytes(ownership.length);
	if (bytes > std::numeric_limits<std::uint64_t>::max() - bytes_)
		throw std::length_error("mapped admission extent overflow");
	rows_.insert(found, Row{ownership, std::move(mapping)});
	bytes_ += bytes;
}
bool MappedRecoveryAdmission::warm() const {
	if (!populated_ || failed_)
		throw std::logic_error("unpopulated mapped admission");
	return warm_;
}
void MappedRecoveryAdmission::bind(Descriptor target, DirectoryIdentity directory,
								   StorageIdentity storage) const {
	warm();
	if (target != target_ || directory != directory_ || storage != storage_)
		throw std::invalid_argument("mapped admission target binding mismatch");
}
void MappedRecoveryAdmission::bind_selection(Descriptor target, DirectoryIdentity directory) const {
	warm();
	if (target != target_ || directory != directory_)
		throw std::invalid_argument("mapped admission selection binding mismatch");
}
void MappedRecoveryAdmission::bind_resources(const std::shared_ptr<d::StorageLedger> &heap,
											 const std::shared_ptr<d::StorageLedger> &mappings,
											 const d::RetirementHandle<LeasedPagedRoot> &prior) const {
	warm();
	if (heap != heap_ || mappings != mappings_)
		throw std::invalid_argument("mapped admission recovery account mismatch");
	if (!warm_)
		return;
	if (!prior || prior->descriptor() != base_ || prior->directory_identity() != directory_ ||
		prior->storage_identity() != storage_ || !prior->registry()->uses_account(heap_))
		throw std::invalid_argument("mapped admission recovery base mismatch");
	for (std::size_t i = 0; i < prior->registry()->inputs().size(); ++i)
		if (prior->registry()->mapping(i)->storage_account() != mappings_)
			throw std::invalid_argument("mapped admission base mapping account mismatch");
}
d::RetirementHandle<GenerationMapping> MappedRecoveryAdmission::take_root() {
	warm();
	if (!root_)
		throw std::logic_error("mapped root already consumed");
	return std::move(root_);
}
d::RetirementHandle<SegmentMapping> MappedRecoveryAdmission::take_segment(const SegmentOwnership &ownership) {
	warm();
	const auto found =
		std::lower_bound(rows_.begin(), rows_.end(), ownership.segment, [](const Row &row, const auto &id) {
			return std::memcmp(row.ownership.segment.data(), id.data(), id.size()) < 0;
		});
	if (found == rows_.end() || found->ownership != ownership || !found->mapping)
		throw std::invalid_argument("mapped segment reservation unavailable");
	return std::move(found->mapping);
}
std::uint64_t MappedRecoveryAdmission::reserved_bytes() const {
	warm();
	return bytes_;
}
std::shared_ptr<RecoveryFile> MappedRecoveryAdmission::take_directory() {
	warm();
	if (std::exchange(directory_taken_, true))
		throw std::logic_error("mapped admission directory already consumed");
	return {directory_hook_, &directory_control_->file};
}
std::shared_ptr<const void> MappedRecoveryAdmission::root_lease() const {
	warm();
	return root_hook_;
}
d::RetirementHandle<SegmentRegistry> MappedRecoveryAdmission::target_registry() const {
	warm();
	return target_registry_;
}
d::RetirementHandle<SegmentRegistry> MappedRecoveryAdmission::fresh_registry() const {
	warm();
	return fresh_registry_;
}
std::shared_ptr<const void> MappedRecoveryAdmission::fresh_owner() const {
	warm();
	return fresh_hook_;
}
d::RetirementHandle<LeasedPagedRoot> MappedRecoveryAdmission::capability() const {
	warm();
	return capability_;
}
std::shared_ptr<const void> MappedRecoveryAdmission::capability_owner() const {
	warm();
	return capability_hook_;
}
ReadView MappedRecoveryAdmission::take_view() {
	warm();
	if (!view_)
		throw std::logic_error("mapped admission view already consumed");
	const auto root = capability_->root();
	const auto counts = capability_->counts();
	view_->install_paged(capability_hook_, {root, capability_->registry()->inputs()}, counts.bools,
						 counts.ints, counts.overrides, counts.strings);
	return ReadView(std::move(view_));
}
bool MappedRecoveryAdmission::retire_step() noexcept {
	view_.reset();
	capability_hook_.reset();
	capability_.reset();
	fresh_hook_.reset();
	fresh_registry_.reset();
	target_registry_.reset();
	root_hook_.reset();
	directory_hook_.reset();
	directory_control_.reset();
	root_.reset();
	for (unsigned count = 0; count < 16 && !rows_.empty(); ++count)
		rows_.pop_back();
	return rows_.empty();
}
} // namespace detent::local::detail
