#pragma once
#include "detent_delivery/coordinator.hh"

namespace detent_agent {
struct ManagedOptions {
	detent::delivery::CoordinatorOptions coordinator;
	detent::local::DiskStoreOptions disk;
	std::size_t heap_bytes = 256 * 1024 * 1024;
	std::size_t mapping_bytes = 512 * 1024 * 1024;
};
// Configuration/startup worker only. Credential files must be privately owned;
// the public reader configuration contains only the generation directory.
ManagedOptions load_managed_options(const std::filesystem::path &configuration,
									const std::filesystem::path &directory, detent::local::StoreOpenMode mode,
									bool system);
} // namespace detent_agent
