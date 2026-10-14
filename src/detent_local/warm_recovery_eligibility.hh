#pragma once
#include "leased_paged_root.hh"
namespace detent::local::detail {
// Admission and recovery share the same actual-base and target eligibility checks.
inline bool eligible_warm_recovery(const distribution::RetirementHandle<LeasedPagedRoot> &prior,
								   const paged::Root &target, DirectoryIdentity directory,
								   StorageIdentity storage,
								   const std::shared_ptr<distribution::StorageLedger> &heap,
								   const std::shared_ptr<distribution::StorageLedger> &mappings) {
	if (!prior || prior->directory_identity() != directory || prior->storage_identity() != storage ||
		!prior->registry()->uses_account(heap) || !prior->root().cursor.same_identity(target.cursor) ||
		target.cursor.revision < prior->root().cursor.revision || target.profile != prior->root().profile ||
		target.namespaces != prior->root().namespaces)
		return false;
	const auto registry = prior->registry();
	for (std::size_t i = 0; i < registry->inputs().size(); ++i)
		if (registry->mapping(i)->storage_account() != mappings)
			return false;
	return true;
}
} // namespace detent::local::detail
