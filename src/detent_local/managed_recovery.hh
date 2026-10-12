#pragma once
#include "recovery_context.hh"
namespace detent::local {
class DiskStore;
namespace detail {
struct StoreCore;
struct ManagedRecoveryContext {
	RecoveryContext legacy, paged;
	std::weak_ptr<StoreCore> owner;
	StorageIdentity storage;
	DirectoryIdentity directory;
};
class ManagedRecovery {
  public:
	// Capture on the store executor; immutable capabilities may run on a worker.
	static ManagedRecoveryContext capture(const DiskStore &);
	static RecoveredGeneration recover(const std::filesystem::path &, ManagedRecoveryContext);
	static void settle_failure(DiskStore &, const ManagedRecoveryContext &, std::exception_ptr);
};
} // namespace detail
} // namespace detent::local
