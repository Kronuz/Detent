#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace detent_cli {

inline constexpr int kDefaultPort = 8317;
using Config = std::map<std::string, std::string>;
enum class FieldKind { String, Path, PositiveInt, NonnegativeInt, PositiveUint64, NonnegativeUint64 };
struct ConfigField { std::string flag; FieldKind kind; bool trusted = false; };
using ConfigSchema = std::map<std::string, ConfigField>;
struct ConfigFile { Config values; std::filesystem::path path; };

std::filesystem::path user_config_dir();
std::filesystem::path system_config_dir();
std::filesystem::path default_runtime_dir(bool system = false);
std::filesystem::path default_state_dir(bool system = false);
std::filesystem::path default_agent_state_dir(bool system = false);
ConfigFile load_config(const std::vector<std::string>& args, const std::string& filename, const ConfigSchema& schema);

} // namespace detent_cli
