#pragma once
#include "../detent_cli/daemon.hh"
#include "managed_config.hh"
#include <chrono>
#include <lifecycle.hh>

namespace detent_agent {
// The process settings of the daemon (server composition, Options).
struct ManagedSettings {
	bool detach = false;
	std::chrono::milliseconds shutdown_timeout{35'000};
};
// Called only inside run_daemon's child/foreground callback, after forking; `signals` is the first
// object the child constructed.
int run_managed(const std::filesystem::path &configuration, const std::filesystem::path &directory,
				detent::local::StoreOpenMode mode, bool system, const ManagedSettings &settings,
				const detent_cli::Ready &ready, task::stop_signals &signals);
} // namespace detent_agent
