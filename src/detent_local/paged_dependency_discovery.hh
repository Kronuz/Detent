#pragma once
#include "segment_registry.hh"
#include <optional>
namespace detent::local::detail {
// Worker-side dependency enumeration; GraphValidator seals semantics afterward.
class DependencyDiscovery {
  public:
	DependencyDiscovery(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementDomain &,
						paged::Root, StorageIdentity, paged::Limits = {});
	DependencyDiscovery(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementFactory,
						paged::Root, StorageIdentity, paged::Limits = {},
						distribution::RetirementHandle<SegmentRegistry> prior = {},
						distribution::RetirementHandle<SegmentRegistry> admitted = {});
	~DependencyDiscovery();
	DependencyDiscovery(DependencyDiscovery &&) noexcept;
	DependencyDiscovery(const DependencyDiscovery &) = delete;
	void step(ValidationBudget &);
	std::optional<StorageIdentity> missing() const;
	void supply(distribution::RetirementHandle<SegmentMapping>);
	bool done() const noexcept;
	std::uint64_t hashed_segment_bytes() const noexcept;
	distribution::RetirementHandle<SegmentRegistry> take();
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
