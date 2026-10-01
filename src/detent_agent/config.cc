#include "config.hh"

#include <algorithm>
#include <stdexcept>

namespace detent_agent {

Config load_config(const std::vector<std::string>& args) {
	using enum detent_cli::FieldKind;
	auto file = detent_cli::load_config(args, "agent.json", {
		{"defaults_path", {"--defaults", Path}},
		{"source_path", {"--source-path", Path}},
		{"source_address", {"--source-address", String}},
		{"source_http", {"--source-address", String}},
		{"runtime_dir", {"--runtime-dir", Path}},
		{"poll_interval_ms", {"--poll-interval-ms", PositiveInt}},
	});
	auto& settings = file.values;
	if (settings.contains("--source-path") && settings.contains("--source-address")) {
		throw std::runtime_error("config " + file.path.string() + ": source_path and source_address are mutually exclusive");
	}
	if (!settings.contains("--defaults")) {
		auto directories = file.path.empty()
			? (std::find(args.begin(), args.end(), "--system") != args.end()
				? std::vector<std::filesystem::path>{detent_cli::system_config_dir()}
				: std::vector<std::filesystem::path>{user_config_dir(), detent_cli::system_config_dir()})
			: std::vector<std::filesystem::path>{file.path.parent_path()};
		for (const auto& dir : directories) {
			auto defaults = dir / "defaults.json";
			if (std::filesystem::exists(defaults)) {
				settings.emplace("--defaults", defaults.string());
				break;
			}
		}
	}
	return std::move(settings);
}

} // namespace detent_agent
