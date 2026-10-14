#pragma once
#include "candidate_paged_validator.hh"
#include "paged_plan.hh"
#include "state_equivalence.hh"
namespace detent::local::detail {
// Reset intent describes construction; only trusted host configuration authorizes publication.
class PagedSnapshotPlanBuilder {
  public:
	PagedSnapshotPlanBuilder(std::shared_ptr<const distribution::ValidatedSnapshot>,
							 std::optional<RecoveredGeneration>, DirectoryIdentity, StorageIdentity,
							 GenerationId, distribution::RetirementFactory,
							 std::shared_ptr<distribution::StorageLedger>, ViewLimits);
	void step(ValidationBudget &);
	bool done() const noexcept;
	bool unchanged() const;
	PagedPlan take();
	StateEquivalenceProof take_unchanged_proof();

  private:
	std::optional<RecoveredGeneration> base_;
	distribution::RetirementFactory factory_;
	std::shared_ptr<distribution::StorageLedger> heap_;
	DirectoryIdentity directory_;
	StorageIdentity storage_;
	ViewLimits limits_;
	std::optional<PagedSnapshotBuilder> encoder_;
	std::optional<CandidatePagedValidator> validator_;
	std::optional<StateEquivalence> equality_;
	std::optional<StateEquivalenceProof> unchanged_proof_;
	distribution::RetirementHandle<PagedPlanState> state_;
	std::size_t copied_ = 0, histogram_ = 0, charge_ = 0;
	bool failed_ = false, finished_ = false;
	bool grant(std::size_t, ValidationBudget &);
};
} // namespace detent::local::detail
