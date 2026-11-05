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
	std::span<const std::uint32_t, 257> override_histogram() const;
	bool uses_account(const std::shared_ptr<distribution::StorageLedger> &account) const noexcept {
		return account && inputs_.get_allocator() == distribution::StorageAllocator<SegmentInput>(account);
	}
	std::shared_ptr<const void> metadata_identity() const {
		validated_root();
		return identity_;
	}
	std::uint64_t physical_bytes() const noexcept { return physical_bytes_; }
	// Non-null from a prior-free discovery until the graph validation that used it records reachability.
	const paged::Root *compared_root() const noexcept { return compared_root_ ? &*compared_root_ : nullptr; }

  protected:
	bool retire_step() noexcept override;

  private:
	// Records that a discovery without a prior registry compared every segment checksum and every page
	// digest reachable from the root, so the graph validation of this recovery need not repeat them.
	void certify_digests(const paged::Root &);
	// A trusted restart records what the build that verified this generation derived (live pages and
	// bytes per segment, the override histogram) instead of walking the graph for it.
	void adopt_derived(const paged::Root &, std::span<const SegmentReachability>,
					   std::span<const std::uint32_t, 257>);
	using MappingHandle = distribution::RetirementHandle<SegmentMapping>;
	std::vector<MappingHandle, distribution::StorageAllocator<MappingHandle>> mappings_;
	std::vector<SegmentInput, distribution::StorageAllocator<SegmentInput>> inputs_;
	std::vector<SegmentReachability, distribution::StorageAllocator<SegmentReachability>> reachable_;
	std::optional<paged::Root> validated_root_, compared_root_;
	std::array<std::uint32_t, 257> override_histogram_{};
	std::shared_ptr<const void> identity_;
	StorageIdentity storage_;
	paged::Limits limits_;
	std::uint64_t physical_bytes_ = 0;
	bool sealed_ = false;
	friend class TrustedRestart;
	friend class DependencyDiscovery;
	friend class GraphValidator;
	friend class WarmGraphValidator;
};
} // namespace detent::local::detail
