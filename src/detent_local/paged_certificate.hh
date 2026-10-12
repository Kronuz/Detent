#pragma once
#include "leased_paged_root.hh"
namespace detent::local::detail {
// Segment metadata certifies one recovered root without retaining ancestors or mappings.
class PagedDependencyCertificate final : public distribution::RetirementNode {
  public:
	PagedDependencyCertificate(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementFactory);
	void capture(const RecoveredGeneration &, const PagedDependencyCertificate *prior = nullptr);
	const Descriptor &descriptor() const;
	const paged::Root &root() const;
	DirectoryIdentity directory_identity() const;
	StorageIdentity storage_identity() const;
	std::span<const SegmentOwnership> dependencies() const;

  protected:
	bool retire_step() noexcept override;

  private:
	struct Dependencies;
	distribution::RetirementHandle<Dependencies> dependencies_;
	std::shared_ptr<distribution::StorageLedger> account_;
	distribution::RetirementFactory factory_;
	Descriptor descriptor_;
	paged::Root root_;
	DirectoryIdentity directory_;
	StorageIdentity storage_;
	bool sealed_ = false, failed_ = false;
};
} // namespace detent::local::detail
