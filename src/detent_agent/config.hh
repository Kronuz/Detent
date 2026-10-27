#pragma once

#include "../detent_cli/config.hh"
#include <cstdint>
#include <chrono>
#include <optional>

namespace detent_agent {
inline constexpr auto managed_startup_timeout = std::chrono::seconds(180);

using detent_cli::Config;
using detent_cli::user_config_dir;
using detent_cli::default_runtime_dir;
Config load_config(const std::vector<std::string>& args);
std::uint32_t publisher_uid(std::optional<std::string> user, bool system);

} // namespace detent_agent
