#pragma once
#include "recovery_context.hh"
namespace detent::local {
class DiskStore;
namespace detail {
struct ManagedRecoveryContext {
	RecoveryContext legacy, paged;
};
class ManagedRecovery {
  public:
	// Capture on the store executor; immutable capabilities may run on a worker.
	static ManagedRecoveryContext capture(const DiskStore &);
	static RecoveredGeneration recover(const std::filesystem::path &, ManagedRecoveryContext);
};
} // namespace detail
} // namespace detent::local
