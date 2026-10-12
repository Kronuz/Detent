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
	if (!heap_ || !mappings_)
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
bool MappedRecoveryAdmission::retire_step() noexcept {
	root_.reset();
	for (unsigned count = 0; count < 16 && !rows_.empty(); ++count)
		rows_.pop_back();
	return rows_.empty();
}
} // namespace detent::local::detail
