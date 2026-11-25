#include "state_equivalence.hh"
namespace detent::local::detail {
StateEquivalence::StateEquivalence(RecoveredGeneration left, RecoveredGeneration right,
								   std::shared_ptr<distribution::StorageLedger> account)
	: source_(left.descriptor()), target_(right.descriptor()), source_directory_(left.directory_identity()),
	  target_directory_(right.directory_identity()) {
	if (!account)
		throw std::invalid_argument("state equivalence requires a storage account");
	equal_ = source_.cursor == target_.cursor && source_.profile == target_.profile &&
			 StateCursor::state_commitment(left) == StateCursor::state_commitment(right);
}
StateEquivalence::StateEquivalence(StateCursor left, StateCursor right)
	: source_(left.descriptor()), target_(right.descriptor()), source_directory_(left.directory_identity()),
	  target_directory_(right.directory_identity()) {
	equal_ = source_.cursor == target_.cursor && source_.profile == target_.profile &&
			 left.state_commitment() == right.state_commitment();
}
StateEquivalenceProof StateEquivalence::take_proof() {
	if (!equal_ || proof_taken_)
		throw std::logic_error("state equivalence proof unavailable");
	proof_taken_ = true;
	return StateEquivalenceProof(source_, target_, source_directory_, target_directory_);
}
} // namespace detent::local::detail
