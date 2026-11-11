#pragma once
#include "detent_local/generation.hh"
#include "response.hh"

namespace detent::local::detail {
class StateEquivalenceProof;
}
namespace detent::delivery {
// Optional owner-local distribution attachment. Staging releases all input
// borrows before disposal; selected() receives verified durable recovery.
class PublicationSink {
  public:
	virtual ~PublicationSink() = default;
	virtual void authenticated() noexcept = 0;
	virtual void begin_stage(const ReceivedUpdate &) = 0;
	virtual bool stage_step(distribution::WorkBudget &) = 0;
	virtual void bind_candidate(const local::Descriptor &, local::DirectoryIdentity) noexcept = 0;
	virtual bool release_stage(distribution::WorkBudget &) noexcept = 0;
	virtual void selected(const local::RecoveredGeneration &) = 0;
	virtual void selected_equivalent(const local::RecoveredGeneration &generation,
									 const local::detail::StateEquivalenceProof &) {
		selected(generation);
	}
	// A repair at the head's own cursor replaced the selected generation with an equal one.
	virtual void selected_repaired(const local::RecoveredGeneration &generation) { selected(generation); }
	virtual void fence() noexcept = 0;
};
} // namespace detent::delivery
