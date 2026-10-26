#pragma once
#include "detent_distribution/protocol.hh"
#include "paged_contextual_reuse.hh"
namespace detent::local::detail {
class PagedPlan;
class PagedPlanState;
class StateEquivalenceProof;
class PagedPlanBuilder {
  public:
    struct Republish {};
    PagedPlanBuilder(Republish, const RecoveredGeneration &, GenerationId,
                     distribution::RetirementDomain &);
    PagedPlanBuilder(Republish, const RecoveredGeneration &, GenerationId,
                     distribution::RetirementFactory, std::shared_ptr<distribution::StorageLedger>);
	PagedPlanBuilder(const RecoveredGeneration &, std::shared_ptr<const distribution::ValidatedDelta>,
					 GenerationId, distribution::RetirementDomain &);
	PagedPlanBuilder(const RecoveredGeneration &, std::shared_ptr<const distribution::ValidatedDelta>,
					 GenerationId, distribution::RetirementFactory,
					 std::shared_ptr<distribution::StorageLedger>);
	~PagedPlanBuilder();
	PagedPlanBuilder(PagedPlanBuilder &&) noexcept;
	PagedPlanBuilder(const PagedPlanBuilder &) = delete;
	void step(ValidationBudget &);
	bool done() const noexcept;
	PagedPlan take();
    std::shared_ptr<const StateEquivalenceProof> evidence() const;
	bool discard(ValidationBudget &);
	static ReuseNavigator reuse(const RecoveredGeneration &, std::shared_ptr<distribution::StorageLedger>,
								std::string_view typed_key, std::size_t frontier_depth = SIZE_MAX);
	static ReuseNavigator reuse_at(const RecoveredGeneration &, std::shared_ptr<distribution::StorageLedger>,
								   paged::Reference, std::string_view entering_prefix);

  private:
	distribution::RetirementHandle<PagedPlanState> plan_;
	bool failed_ = false;
};
} // namespace detent::local::detail
