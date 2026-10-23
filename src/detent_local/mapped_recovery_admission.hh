#pragma once
#include "generation_mapping.hh"
#include "leased_paged_root.hh"
#include "read_root.hh"
#include <atomic>
#include <exception>
namespace detent::local::detail {
class PagedPlan;
// Owns staged controls and their real mapping tickets before publication IO.
class MappedRecoveryAdmission final : public distribution::RetirementNode {
  public:
	MappedRecoveryAdmission(std::shared_ptr<distribution::StorageLedger> heap,
							std::shared_ptr<distribution::StorageLedger> mappings,
							distribution::RetirementFactory);
	void populate(const PagedPlan &, distribution::RetirementHandle<LeasedPagedRoot> prior,
				  ViewLimits limits = {});
	bool warm() const;
	void bind(Descriptor, DirectoryIdentity, StorageIdentity) const;
	void bind_selection(Descriptor, DirectoryIdentity) const;
	void bind_resources(const std::shared_ptr<distribution::StorageLedger> &heap,
						const std::shared_ptr<distribution::StorageLedger> &mappings,
						const distribution::RetirementHandle<LeasedPagedRoot> &prior) const;
	distribution::RetirementHandle<GenerationMapping> take_root();
	distribution::RetirementHandle<SegmentMapping> take_segment(const SegmentOwnership &);
	std::uint64_t reserved_bytes() const;
	std::shared_ptr<RecoveryFile> take_directory();
	std::shared_ptr<const void> root_lease() const;
	distribution::RetirementHandle<SegmentRegistry> target_registry() const;
	distribution::RetirementHandle<SegmentRegistry> fresh_registry() const;
	std::shared_ptr<const void> fresh_owner() const;
	distribution::RetirementHandle<LeasedPagedRoot> capability() const;
	std::shared_ptr<const void> capability_owner() const;
	ReadView take_view();
	void claim_attempt();
	void complete_attempt();
	void fail_attempt(std::exception_ptr) noexcept;
	std::exception_ptr failure() const noexcept;

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
	distribution::RetirementHandle<GenerationDirectory> directory_control_;
	std::shared_ptr<distribution::RetirementHandle<GenerationDirectory>> directory_hook_;
	std::shared_ptr<distribution::RetirementHandle<GenerationMapping>> root_hook_;
	distribution::RetirementHandle<SegmentRegistry> target_registry_, fresh_registry_;
	std::shared_ptr<distribution::RetirementHandle<SegmentRegistry>> fresh_hook_;
	distribution::RetirementHandle<LeasedPagedRoot> capability_;
	std::shared_ptr<distribution::RetirementHandle<LeasedPagedRoot>> capability_hook_;
	distribution::RetirementHandle<ReadView::Root> view_;
	Descriptor target_;
	std::optional<Descriptor> base_;
	DirectoryIdentity directory_;
	StorageIdentity storage_;
	PagedStateCounts expected_counts_;
	std::uint64_t bytes_ = 0;
	bool populated_ = false, failed_ = false, warm_ = false;
	bool directory_taken_ = false;
	enum class Attempt { Available, Running, Recovered, Failed };
	std::atomic<Attempt> attempt_{Attempt::Available};
	std::exception_ptr failure_;
	void append(SegmentOwnership);
};
} // namespace detent::local::detail
