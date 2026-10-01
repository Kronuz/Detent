#pragma once

#include "../detent_cli/config.hh"

namespace detent_agent {

using detent_cli::Config;
using detent_cli::user_config_dir;
using detent_cli::default_runtime_dir;
Config load_config(const std::vector<std::string>& args);

} // namespace detent_agent
