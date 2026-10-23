#include "config.hh"

#include "detent/json_schema_internal.hh"
#include <rapidjson/document.h>

#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
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

std::filesystem::path default_agent_state_dir(bool system) {
	if (!system)
		return default_state_dir(false) / "agent";
#ifdef __linux__
	return "/var/lib/detent-agent";
#else
	return "/var/db/detent-agent";
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
	struct File {
		int fd;
		~File() { if (fd >= 0) ::close(fd); }
	} file{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW)};
	const bool no_symlink = file.fd >= 0;
	if (file.fd < 0 && errno == ELOOP)
		file.fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (file.fd < 0) fail("cannot open file");
	struct stat before{}, after{};
	const bool inspected = ::fstat(file.fd, &before) == 0;
	std::string bytes(64 * 1024 + 1, '\0');
	std::size_t used = 0;
	while (used != bytes.size()) {
		const auto n = ::read(file.fd, bytes.data() + used, bytes.size() - used);
		if (n < 0 && errno == EINTR) continue;
		if (n < 0) fail("cannot read file");
		if (!n) break;
		used += static_cast<std::size_t>(n);
	}
	bytes.resize(used);
	if (bytes.size() > 64 * 1024) fail("file exceeds 64 KiB");
	const bool trusted = no_symlink && inspected && ::fstat(file.fd, &after) == 0 &&
		S_ISREG(before.st_mode) && (before.st_uid == 0 || before.st_uid == ::geteuid()) &&
		!(before.st_mode & 0022) && !(after.st_mode & 0022) &&
		after.st_uid == before.st_uid && before.st_size == after.st_size &&
		before.st_size >= 0 && static_cast<std::uint64_t>(before.st_size) == bytes.size();
	if (detent::internal::nesting_depth_exceeds(bytes, 16)) fail("JSON nesting exceeds limit");
	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
	if (doc.HasParseError() || !doc.IsObject()) fail("expected a JSON object");

	Config settings;
	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
		std::string key(it->name.GetString(), it->name.GetStringLength());
		auto field = schema.find(key);
		if (field == schema.end()) fail("unknown field: " + key);
		if (field->second.trusted && !trusted) fail("reader capacity requires a trusted regular configuration file");
		const auto& flag = field->second.flag;
		if (settings.contains(flag)) fail("duplicate field: " + key);
		std::string value;
		if (field->second.kind == FieldKind::PositiveInt || field->second.kind == FieldKind::NonnegativeInt) {
			const bool positive = field->second.kind == FieldKind::PositiveInt;
			if (!it->value.IsInt() || it->value.GetInt() < (positive ? 1 : 0)) fail(key + (positive ? " must be a positive integer" : " must be a nonnegative integer"));
			value = std::to_string(it->value.GetInt());
		} else if (field->second.kind == FieldKind::PositiveUint64 || field->second.kind == FieldKind::NonnegativeUint64) {
			if (!it->value.IsUint64() || (field->second.kind == FieldKind::PositiveUint64 && !it->value.GetUint64()))
				fail(key + " must be an unsigned capacity integer");
			value = std::to_string(it->value.GetUint64());
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
