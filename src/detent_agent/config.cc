#include "config.hh"
#include "reader_capacity.hh"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <pwd.h>
#include <unistd.h>
#include <stdexcept>

namespace detent_agent {

std::uint32_t publisher_uid(std::optional<std::string> user, bool system) {
	if (!user && !system) return ::geteuid();
	if (!user) {
#ifdef __APPLE__
		user = "_detent";
#else
		user = "detent";
#endif
	}
	if (user->empty() || user->size() > 255 || user->find('\0') != std::string::npos)
		throw std::invalid_argument("invalid publisher user");
	if (std::all_of(user->begin(), user->end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
		std::uint32_t id = 0;
		const auto [end, error] = std::from_chars(user->data(), user->data() + user->size(), id);
		if (error != std::errc{} || end != user->data() + user->size() || id == UINT32_MAX)
			throw std::invalid_argument("publisher UID exceeds supported width");
		return id;
	}
	const auto suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
	constexpr std::size_t maximum = 1024 * 1024;
	std::vector<char> buffer(std::clamp<std::size_t>(suggested > 0 ? suggested : 1024, 1024, maximum));
	for (;;) {
		struct passwd entry{}, *result = nullptr;
		const int error = ::getpwnam_r(user->c_str(), &entry, buffer.data(), buffer.size(), &result);
		if (!error && result) return result->pw_uid;
		if (error != ERANGE || buffer.size() == maximum)
			throw std::runtime_error("cannot resolve publisher user: " + *user);
		buffer.resize(std::min(maximum, buffer.size() * 2));
	}
}

Config load_config(const std::vector<std::string>& args) {
	using enum detent_cli::FieldKind;
	detent_cli::ConfigSchema schema =
										{
											{"log_level", {"--log-level", String}},
											{"verbosity", {"--verbosity", NonnegativeInt}},
											{"color", {"--color", String}},
											{"runtime_dir", {"--runtime-dir", Path}},
											{"managed_path", {"--managed", Path}},
											{"generation_dir", {"--generation-dir", Path}},
											{"publisher_user", {"--publisher-user", StringOrUint64, true}},
											{"reader_validation", {"--reader-validation", String, true}},
											{"generation_cluster", {"--generation-cluster", String}},
											{"generation_group", {"--generation-group", String}},
											{"generation_epoch", {"--generation-epoch", String}},
											{"generation_scope", {"--generation-scope", String}},
										};
	for (auto field : reader_fields) {
		std::string suffix(field);
		std::replace(suffix.begin(), suffix.end(), '_', '-');
		schema.emplace("reader_" + std::string(field), detent_cli::ConfigField{
			"--reader-" + suffix, field == "overrides" ? NonnegativeUint64 : PositiveUint64, true});
	}
	auto file = detent_cli::load_config(args, "agent.json", schema);
	auto& settings = file.values;
	return std::move(settings);
}

} // namespace detent_agent
