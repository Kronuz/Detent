#include "segment_registry.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace detent::local::detail {
SegmentRegistry::SegmentRegistry(std::shared_ptr<distribution::StorageLedger> account,
								 paged::Limits limits)
	: mappings_(distribution::StorageAllocator<MappingHandle>(account)),
	  inputs_(distribution::StorageAllocator<SegmentInput>(account)),
	  reachable_(distribution::StorageAllocator<SegmentReachability>(account)),
	  limits_(limits) {
	if (limits.segments > mappings_.max_size() || limits.segments > inputs_.max_size() ||
		limits.segments > reachable_.max_size())
		throw std::invalid_argument("invalid segment registry admission");
	identity_ =
		std::allocate_shared<unsigned char>(distribution::StorageAllocator<unsigned char>(account), 0);
	mappings_.reserve(limits.segments);
	inputs_.reserve(limits.segments);
	reachable_.reserve(limits.segments);
}
void SegmentRegistry::append(MappingHandle mapping) {
	if (sealed_)
		throw std::logic_error("sealed segment registry");
	if (!mapping || !mapping->has_ownership() || inputs_.size() == limits_.segments ||
		mapping->ownership().length > limits_.bytes - physical_bytes_ ||
		(!inputs_.empty() &&
		 std::memcmp(inputs_.back().ownership.segment.data(), mapping->ownership().segment.data(), 16) >= 0))
		throw std::invalid_argument("invalid segment registry entry");
	inputs_.push_back({mapping->ownership(), mapping->bytes(), mapping->first_touch()});
	reachable_.push_back({});
	physical_bytes_ += mapping->ownership().length;
	mappings_.push_back(std::move(mapping));
}
void SegmentRegistry::seal() {
	if (sealed_)
		throw std::logic_error("sealed segment registry");
	sealed_ = true;
}
void SegmentRegistry::certify_digests(const paged::Root &root) {
	if (!sealed_ || compared_root_)
		throw std::logic_error("segment registry digests cannot be certified");
	compared_root_ = root;
}
void SegmentRegistry::adopt_derived(const paged::Root &root, std::span<const SegmentReachability> live,
									std::span<const std::uint32_t, 257> histogram) {
	if (!sealed_ || validated_root_ || live.size() != reachable_.size() || root.segments != reachable_.size())
		throw std::logic_error("segment registry cannot adopt derived state");
	std::copy(live.begin(), live.end(), reachable_.begin());
	std::copy(histogram.begin(), histogram.end(), override_histogram_.begin());
	validated_root_ = root;
}
std::span<const SegmentInput> SegmentRegistry::inputs() const {
	if (!sealed_)
		throw std::logic_error("unsealed segment registry");
	return inputs_;
}
const distribution::RetirementHandle<SegmentMapping> &SegmentRegistry::mapping(std::size_t index) const {
	inputs();
	if (index >= mappings_.size())
		throw std::out_of_range("segment mapping index");
	return mappings_[index];
}
SegmentReachability SegmentRegistry::reachability(std::size_t index) const {
	validated_root();
	if (index >= reachable_.size())
		throw std::out_of_range("segment reachability index");
	return reachable_[index];
}
std::span<const std::uint32_t, 257> SegmentRegistry::override_histogram() const {
	validated_root();
	return override_histogram_;
}
const paged::Root &SegmentRegistry::validated_root() const {
	if (!validated_root_)
		throw std::logic_error("segment graph is not validated");
	return *validated_root_;
}
bool SegmentRegistry::retire_step() noexcept {
	for (unsigned i = 0; i < 16 && !mappings_.empty(); ++i) {
		inputs_.pop_back();
		reachable_.pop_back();
		mappings_.pop_back();
	}
	return mappings_.empty();
}
} // namespace detent::local::detail
