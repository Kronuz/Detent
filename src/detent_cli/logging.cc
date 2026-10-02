#include "logging.hh"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace detent_logging {
Settings parse(std::vector<std::string>& args, const detent_cli::Config& config) {
	auto values = config;
	std::vector<std::string> remaining;
	unsigned verbose = 0;
	for (std::size_t i = 0; i < args.size(); ++i) {
		auto arg = args[i];
		if (arg.size() >= 2 && arg[0] == '-' && arg.find_first_not_of('v', 1) == std::string::npos) {
			verbose = std::min<unsigned>(2, verbose + std::min<std::size_t>(2, arg.size() - 1));
		} else if (arg == "--no-color") {
			values["--color"] = "never";
		} else if (arg == "--log-level" || arg == "--color" || arg == "--verbosity") {
			if (++i >= args.size() || args[i].starts_with('-')) throw std::invalid_argument(arg + " requires a value");
			values[arg] = args[i];
		} else if (arg.starts_with("--log-level=") || arg.starts_with("--color=") || arg.starts_with("--verbosity=")) {
			auto equal = arg.find('=');
			values[arg.substr(0, equal)] = arg.substr(equal + 1);
		} else {
			remaining.push_back(arg);
		}
	}
	Settings result;
	if (auto it = values.find("--log-level"); it != values.end()) {
		const std::map<std::string, int> levels{{"emergency",LOG_EMERG},{"alert",LOG_ALERT},{"critical",LOG_CRIT},{"error",LOG_ERR},{"warning",LOG_WARNING},{"notice",LOG_NOTICE},{"info",LOG_INFO},{"debug",LOG_DEBUG}};
		auto found = levels.find(it->second);
		if (found == levels.end()) throw std::invalid_argument("invalid log level: " + it->second);
		result.level = found->second;
	}
	unsigned verbosity = 0;
	if (auto it = values.find("--verbosity"); it != values.end()) {
		auto [end, ec] = std::from_chars(it->second.data(), it->second.data()+it->second.size(), verbosity);
		if (ec != std::errc{} || end != it->second.data()+it->second.size() || verbosity > 2) throw std::invalid_argument("verbosity must be 0..2");
	}
	if (verbose || verbosity) result.level = std::min<int>(MAX_LOG_PRIORITY, result.level + verbose + verbosity);
	if (auto it = values.find("--color"); it != values.end()) {
		if (it->second == "auto") result.color = LogColorMode::automatic;
		else if (it->second == "always") result.color = LogColorMode::always;
		else if (it->second == "never") result.color = LogColorMode::never;
		else throw std::invalid_argument("color must be auto, always, or never");
	}
	args = std::move(remaining);
	return result;
}
Session::Session(Settings settings) {
	Logging::config.log_level = settings.level;
	Logging::config.color = settings.color;
	Logging::config.timestamp_gradient = true;
	Logging::config.iterm2 = false;
	Logging::config.max_pending = 4096;
	Logging::add_handler(std::make_unique<StderrLogger>());
}
Session::~Session() {
	try { Logging::finish(); Logging::join(); } catch (...) { /* Shutdown must not throw. */ }
}
}
