#pragma once
#include "../detent_cli/daemon.hh"
#include <chrono>
#include <filesystem>
#include <lifecycle.hh>
#include <optional>

namespace detent::relay {
// The process settings of the daemon (server composition, Options): limits and the stop bound.
struct HostSettings {
	std::filesystem::path configuration;
	bool detach = false;
	std::size_t reactors = 2;
	std::optional<std::size_t> max_clients, max_in_flight; // default: the configuration's connection bound
	std::chrono::milliseconds idle_timeout{75'000}, shutdown_timeout{35'000};
};
int run_host(const HostSettings &, const detent_cli::Ready &, task::stop_signals &);
} // namespace detent::relay
