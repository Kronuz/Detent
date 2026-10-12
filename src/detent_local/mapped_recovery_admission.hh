#pragma once
#include "generation_mapping.hh"
#include "leased_paged_root.hh"
namespace detent::local::detail {
class PagedPlan;
// Owns staged controls and their real mapping tickets before publication IO.
class MappedRecoveryAdmission final : public distribution::RetirementNode {
  public:
	MappedRecoveryAdmission(std::shared_ptr<distribution::StorageLedger> heap,
							std::shared_ptr<distribution::StorageLedger> mappings,
							distribution::RetirementFactory);
	void populate(const PagedPlan &, distribution::RetirementHandle<LeasedPagedRoot> prior);
	bool warm() const;
	void bind(Descriptor, DirectoryIdentity, StorageIdentity) const;
	distribution::RetirementHandle<GenerationMapping> take_root();
	distribution::RetirementHandle<SegmentMapping> take_segment(const SegmentOwnership &);
	std::uint64_t reserved_bytes() const;

  protected:
	bool retire_step() noexcept override;

  private:
	struct Row {
		SegmentOwnership ownership;
		distribution::RetirementHandle<SegmentMapping> mapping;
	};
	std::vector<Row, distribution::StorageAllocator<Row>> rows_;
	std::shared_ptr<distribution::StorageLedger> heap_, mappings_;
	distribution::RetirementFactory factory_;
	distribution::RetirementHandle<GenerationMapping> root_;
	Descriptor target_, base_;
	DirectoryIdentity directory_;
	StorageIdentity storage_;
	std::uint64_t bytes_ = 0;
	bool populated_ = false, failed_ = false, warm_ = false;
	void append(SegmentOwnership);
};
} // namespace detent::local::detail
