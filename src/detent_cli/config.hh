#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace detent_cli {

inline constexpr int kDefaultPort = 52617;
using Config = std::map<std::string, std::string>;
enum class FieldKind { String, Path, PositiveInt };
struct ConfigField { std::string flag; FieldKind kind; };
using ConfigSchema = std::map<std::string, ConfigField>;
struct ConfigFile { Config values; std::filesystem::path path; };

std::filesystem::path user_config_dir();
std::filesystem::path system_config_dir();
std::filesystem::path default_runtime_dir();
std::filesystem::path default_state_dir();
ConfigFile load_config(const std::vector<std::string>& args, const std::string& filename, const ConfigSchema& schema);

} // namespace detent_cli
