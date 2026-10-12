#pragma once
#include "mapped_recovery_admission.hh"
namespace detent::local::detail {
struct RecoveryMetrics {
	std::uint64_t root_hash_bytes = 0, segment_hash_bytes = 0;
	std::uint64_t root_mapping_installs = 0, segment_mapping_installs = 0;
	std::uint64_t definitions_validated = 0, old_definition_headers = 0;
	std::uint64_t sampled_heap_peak = 0, sampled_mapping_peak = 0;
	bool warm = false, progress = false;
};
struct RecoveryContext {
	distribution::RetirementFactory factory;
	std::shared_ptr<distribution::StorageLedger> heap, mappings;
	ViewLimits limits;
	distribution::RetirementHandle<LeasedPagedRoot> prior;
	distribution::RetirementHandle<MappedRecoveryAdmission> admission;
	ValidationBudget budget;
	std::optional<GenerationSelection> selection;
	std::optional<unsigned> layout_version;
	std::shared_ptr<RecoveryMetrics> metrics;
};
class RecoveryEngine {
  public:
	static RecoveredGeneration recover(const std::filesystem::path &, RecoveryContext,
									   std::optional<distribution::Cursor> = {});
	static RecoveredGeneration recover_routed(const std::filesystem::path &, RecoveryContext legacy,
											  RecoveryContext paged,
											  std::optional<distribution::Cursor> = {});
};
} // namespace detent::local::detail
