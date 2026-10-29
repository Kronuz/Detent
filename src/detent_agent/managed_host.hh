#pragma once
#include "../detent_cli/daemon.hh"
#include "managed_config.hh"

namespace detent_agent {
int verify_legacy_metadata(const std::filesystem::path &directory);
// Called only inside run_daemon's child/foreground callback, after forking.
int run_managed(const std::filesystem::path &configuration, const std::filesystem::path &directory,
				detent::local::StoreOpenMode mode, bool system, const detent_cli::Ready &ready);
} // namespace detent_agent
