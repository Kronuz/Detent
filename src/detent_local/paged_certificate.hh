#pragma once
#include "leased_paged_root.hh"
namespace detent::local::detail {
class PagedNamedRoot;
// Segment metadata certifies one recovered root without retaining ancestors or mappings.
class PagedDependencyCertificate final : public distribution::RetirementNode {
  public:
	PagedDependencyCertificate(std::shared_ptr<distribution::StorageLedger>, distribution::RetirementFactory);
	void capture(const RecoveredGeneration &, const PagedDependencyCertificate *prior = nullptr);
	void capture(const LeasedPagedRoot &, const PagedDependencyCertificate *prior = nullptr);
	void begin_recovered(const LeasedPagedRoot &);
	void begin_named(const PagedNamedRoot &, const GraphValidator &);
	void capture_step(ValidationBudget &);
	bool sealed() const noexcept { return sealed_; }
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
	std::shared_ptr<const void> capture_owner_;
	distribution::RetirementHandle<SegmentRegistry> capture_registry_;
	std::span<const SegmentInput> capture_inputs_;
	std::size_t capture_index_ = 0, capture_charge_ = 0;
	bool capturing_ = false;
	bool sealed_ = false, failed_ = false;
};
} // namespace detent::local::detail
