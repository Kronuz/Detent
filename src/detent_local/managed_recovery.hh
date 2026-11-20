#pragma once
#include "recovery_context.hh"
namespace detent::local {
class DiskStore;
namespace detail {
struct StoreCore;
struct ManagedRecoveryContext {
	RecoveryContext paged;
	std::weak_ptr<StoreCore> owner;
	DirectoryIdentity directory;
};
class ManagedRecovery {
  public:
	// Capture on the store executor; immutable capabilities may run on a worker.
	// `name`: the namespace's entry; empty: the table's only entry.
	static ManagedRecoveryContext capture(const DiskStore &, std::string_view name = {});
	static RecoveredGeneration recover(const std::filesystem::path &, ManagedRecoveryContext);
	static void settle_failure(DiskStore &, const ManagedRecoveryContext &, std::exception_ptr);
};
} // namespace detail
} // namespace detent::local
