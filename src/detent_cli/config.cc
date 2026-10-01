#include "config.hh"

#include "detent/json_schema_internal.hh"
#include <rapidjson/document.h>

#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace detent_cli {

std::filesystem::path user_config_dir() {
	if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg && std::filesystem::path(xdg).is_absolute()) {
		return std::filesystem::path(xdg) / "detent";
	}
	if (const char* home = std::getenv("HOME"); home && *home) {
#ifdef __APPLE__
		return std::filesystem::path(home) / "Library" / "Application Support" / "Detent";
#else
		return std::filesystem::path(home) / ".config" / "detent";
#endif
	}
	throw std::runtime_error("cannot locate user config directory: HOME is unset");
}

std::filesystem::path default_runtime_dir(bool system) {
	if (system) {
#ifdef __linux__
		return "/run/detent";
#else
		return "/var/run/detent";
#endif
	}
	if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg && std::filesystem::path(xdg).is_absolute()) {
		return std::filesystem::path(xdg) / "detent";
	}
	// A private per-user temporary directory is the portable fallback when
	// no session manager supplies XDG_RUNTIME_DIR. Honor TMPDIR on all OSes.
	return std::filesystem::temp_directory_path() / ("detent-" + std::to_string(::geteuid()));
}

std::filesystem::path default_state_dir(bool system) {
	if (system) {
#ifdef __linux__
		return "/var/lib/detent";
#else
		return "/var/db/detent";
#endif
	}
	if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg && std::filesystem::path(xdg).is_absolute()) {
		return std::filesystem::path(xdg) / "detent";
	}
	if (const char* home = std::getenv("HOME"); home && *home) {
#ifdef __APPLE__
		return std::filesystem::path(home) / "Library" / "Application Support" / "Detent" / "state";
#else
		return std::filesystem::path(home) / ".local" / "state" / "detent";
#endif
	}
	throw std::runtime_error("cannot locate state directory: HOME is unset");
}

std::filesystem::path system_config_dir() {
#ifdef __APPLE__
	return "/Library/Application Support/Detent";
#elif defined(__FreeBSD__)
	return "/usr/local/etc/detent";
#else
	return "/etc/detent";
#endif
}

namespace {

std::filesystem::path config_path(const std::vector<std::string>& args, const std::string& filename, bool& explicit_path) {
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] != "--config") continue;
		if (i + 1 == args.size() || args[i + 1].starts_with("--")) {
			throw std::runtime_error("--config requires a path");
		}
		explicit_path = true;
		return args[i + 1];
	}
	if (std::find(args.begin(), args.end(), "--system") != args.end()) return system_config_dir() / filename;
	auto user_path = user_config_dir() / filename;
	if (std::filesystem::exists(user_path)) return user_path;
	return system_config_dir() / filename;
}

} // namespace

ConfigFile load_config(const std::vector<std::string>& args, const std::string& filename, const ConfigSchema& schema) {
	bool explicit_path = false;
	auto path = config_path(args, filename, explicit_path);
	if (!explicit_path && !std::filesystem::exists(path)) return {};
	auto fail = [&](const std::string& message) -> void {
		throw std::runtime_error("config " + path.string() + ": " + message);
	};
	std::ifstream in(path, std::ios::binary);
	if (!in) fail("cannot open file");
	// Bound both allocation and parser nesting for user-supplied settings.
	std::string bytes(64 * 1024 + 1, '\0');
	in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	bytes.resize(static_cast<std::size_t>(in.gcount()));
	if (in.bad()) fail("cannot read file");
	if (bytes.size() > 64 * 1024) fail("file exceeds 64 KiB");
	if (detent::internal::nesting_depth_exceeds(bytes, 16)) fail("JSON nesting exceeds limit");
	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseIterativeFlag>(bytes.data(), bytes.size());
	if (doc.HasParseError() || !doc.IsObject()) fail("expected a JSON object");

	Config settings;
	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
		std::string key(it->name.GetString(), it->name.GetStringLength());
		auto field = schema.find(key);
		if (field == schema.end()) fail("unknown field: " + key);
		const auto& flag = field->second.flag;
		if (settings.contains(flag)) fail("duplicate field: " + key);
		std::string value;
		if (field->second.kind == FieldKind::PositiveInt) {
			if (!it->value.IsInt() || it->value.GetInt() <= 0) fail(key + " must be a positive integer");
			value = std::to_string(it->value.GetInt());
		} else {
			if (!it->value.IsString() || it->value.GetStringLength() == 0) fail(key + " must be a nonempty string");
			value.assign(it->value.GetString(), it->value.GetStringLength());
			if (value.find('\0') != std::string::npos) fail(key + " contains a NUL byte");
			if (field->second.kind == FieldKind::Path) {
				value = std::filesystem::absolute(path.parent_path() / value).lexically_normal().string();
			}
		}
		settings.emplace(flag, std::move(value));
	}
	return {std::move(settings), std::filesystem::absolute(path)};
}

} // namespace detent_cli
