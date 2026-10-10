#pragma once
#include "detent_delivery/coordinator.hh"

namespace detent::delivery {
struct ManagedOptions {
	std::string principal;
	std::uint64_t policy_epoch = 1;
	bool delivery_mode_explicit = false;
	detent::delivery::CoordinatorOptions coordinator;
	detent::local::DiskStoreOptions disk;
	std::size_t heap_bytes = 256 * 1024 * 1024;
	std::size_t mapping_bytes = 512 * 1024 * 1024;
};
// Startup worker only; accepts a single-link regular file owned by root or the
// effective user, with no group write or other access, bounded to 1 MiB.
std::string read_private_configuration(const std::filesystem::path &configuration);

// Configuration/startup worker only. Credential files must be privately owned;
// the public reader configuration contains only the generation directory.
ManagedOptions load_managed_options(const std::filesystem::path &configuration,
									const std::filesystem::path &directory, detent::local::StoreOpenMode mode,
									bool system);
} // namespace detent::delivery
