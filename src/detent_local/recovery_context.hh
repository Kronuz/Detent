#pragma once
#include "mapped_recovery_admission.hh"
namespace detent::local::detail {
struct RecoveryContext {
	distribution::RetirementFactory factory;
	std::shared_ptr<distribution::StorageLedger> heap, mappings;
	ViewLimits limits;
	distribution::RetirementHandle<LeasedPagedRoot> prior;
	distribution::RetirementHandle<MappedRecoveryAdmission> admission;
};
class RecoveryEngine {
  public:
	static RecoveredGeneration recover(const std::filesystem::path &, RecoveryContext,
									   std::optional<distribution::Cursor> = {});
};
} // namespace detent::local::detail
