#pragma once
#include "paged_graph_validator.hh"
#include "segment_mapping.hh"
#include <vector>

namespace detent::local::detail {
// Immutable admitted mappings; sealing metadata does not validate the graph.
class SegmentRegistry final : public distribution::RetirementNode {
  public:
	SegmentRegistry(std::shared_ptr<distribution::StorageLedger>, StorageIdentity, paged::Limits = {});
	void append(distribution::RetirementHandle<SegmentMapping>);
	void seal();
	std::span<const SegmentInput> inputs() const;
	const distribution::RetirementHandle<SegmentMapping> &mapping(std::size_t index) const;
	SegmentReachability reachability(std::size_t index) const;
	const paged::Root &validated_root() const;
	std::uint64_t physical_bytes() const noexcept { return physical_bytes_; }

  protected:
	bool retire_step() noexcept override;

  private:
	using MappingHandle = distribution::RetirementHandle<SegmentMapping>;
	std::vector<MappingHandle, distribution::StorageAllocator<MappingHandle>> mappings_;
	std::vector<SegmentInput, distribution::StorageAllocator<SegmentInput>> inputs_;
	std::vector<SegmentReachability, distribution::StorageAllocator<SegmentReachability>> reachable_;
	std::optional<paged::Root> validated_root_;
	StorageIdentity storage_;
	paged::Limits limits_;
	std::uint64_t physical_bytes_ = 0;
	bool sealed_ = false;
	friend class GraphValidator;
};
} // namespace detent::local::detail
