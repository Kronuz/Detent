#pragma once
#include "candidate_paged_validator.hh"
#include "paged_plan.hh"
#include "state_equivalence.hh"
namespace detent::local::detail {
// Full replacement proves same-cursor equality before granting maintenance intent.
class PagedMaintenanceBuilder {
  public:
	PagedMaintenanceBuilder(RecoveredGeneration, distribution::RetirementFactory,
							std::shared_ptr<distribution::StorageLedger>, StorageIdentity, GenerationId,
							ViewLimits);
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedPlan take();
	std::shared_ptr<const StateEquivalenceProof> evidence() const;

  private:
	RecoveredGeneration base_;
	distribution::RetirementFactory factory_;
	std::shared_ptr<distribution::StorageLedger> heap_;
	StorageIdentity storage_;
	ViewLimits limits_;
	std::optional<PagedSnapshotBuilder> encoder_;
	std::optional<CandidatePagedValidator> validator_;
	std::optional<StateEquivalence> equality_;
	distribution::RetirementHandle<PagedPlanState> state_;
	std::size_t copied_ = 0, histogram_ = 0, charge_ = 0;
	bool failed_ = false, sealed_ = false;
	bool grant(std::size_t, ValidationBudget &);
};
} // namespace detent::local::detail
