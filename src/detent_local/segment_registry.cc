#include "segment_registry.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace detent::local::detail {
SegmentRegistry::SegmentRegistry(std::shared_ptr<distribution::StorageLedger> account,
								 StorageIdentity storage, paged::Limits limits)
	: mappings_(distribution::StorageAllocator<MappingHandle>(account)),
	  inputs_(distribution::StorageAllocator<SegmentInput>(account)),
	  reachable_(distribution::StorageAllocator<SegmentReachability>(account)), storage_(storage),
	  limits_(limits) {
	if (!std::any_of(storage.begin(), storage.end(), [](char c) { return c != 0; }) ||
		limits.segments > paged::Limits{}.segments || limits.bytes > paged::Limits{}.bytes)
		throw std::invalid_argument("invalid segment registry admission");
	mappings_.reserve(limits.segments);
	inputs_.reserve(limits.segments);
	reachable_.reserve(limits.segments);
}
void SegmentRegistry::append(MappingHandle mapping) {
	if (sealed_)
		throw std::logic_error("sealed segment registry");
	if (!mapping || inputs_.size() == limits_.segments || mapping->ownership().storage != storage_ ||
		mapping->ownership().length > limits_.bytes - physical_bytes_ ||
		(!inputs_.empty() &&
		 std::memcmp(inputs_.back().ownership.segment.data(), mapping->ownership().segment.data(), 16) >= 0))
		throw std::invalid_argument("invalid segment registry entry");
	inputs_.push_back({mapping->ownership(), mapping->bytes()});
	reachable_.push_back({});
	physical_bytes_ += mapping->ownership().length;
	mappings_.push_back(std::move(mapping));
}
void SegmentRegistry::seal() {
	if (sealed_)
		throw std::logic_error("sealed segment registry");
	sealed_ = true;
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
