#pragma once
#include "mapped_recovery_admission.hh"
namespace detent::local::detail {
struct RecoveryMetrics {
	std::uint64_t root_hash_bytes = 0, segment_hash_bytes = 0;
	std::uint64_t root_mapping_installs = 0, segment_mapping_installs = 0;
	std::uint64_t definitions_validated = 0, old_definition_headers = 0;
	std::uint64_t sampled_heap_peak = 0, sampled_mapping_peak = 0;
	bool warm = false, progress = false, trusted = false;
};
struct RecoveryContext {
	distribution::RetirementFactory factory;
	std::shared_ptr<distribution::StorageLedger> heap, mappings;
	ViewLimits limits;
	distribution::RetirementHandle<LeasedPagedRoot> prior;
	distribution::RetirementHandle<MappedRecoveryAdmission> admission;
	ValidationBudget budget;
	std::optional<GenerationSelection> selection;
	std::shared_ptr<RecoveryMetrics> metrics;
	// The agent's cold start: trust the generation's live record, and refuse a generation without one.
	bool trusted_start = false;
};
class RecoveryEngine {
  public:
	static RecoveredGeneration recover(const std::filesystem::path &, RecoveryContext,
									   std::optional<distribution::NamespaceCursor> = {});
	// The selected generation, bound to its lease; with an admission, the admission's own selection.
	static RecoveredGeneration recover_selected(const std::filesystem::path &, RecoveryContext,
												std::optional<distribution::NamespaceCursor> = {});
};
} // namespace detent::local::detail
