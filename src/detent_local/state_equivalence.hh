#pragma once
#include "state_cursor.hh"
#include "state_equivalence_proof.hh"
namespace detent::local::detail {
// Two states are equal when cursor, profile and state commitment are (a paged encoding is canonical).
class StateEquivalence {
  public:
	StateEquivalence(RecoveredGeneration, RecoveredGeneration, std::shared_ptr<distribution::StorageLedger>);
	StateEquivalence(StateCursor, StateCursor);
	StateEquivalenceProof take_proof();
	bool equal() const noexcept { return equal_; }

  private:
	Descriptor source_, target_;
	std::optional<DirectoryIdentity> source_directory_, target_directory_;
	bool proof_taken_ = false, equal_ = false;
};
} // namespace detent::local::detail
