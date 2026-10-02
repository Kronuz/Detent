#pragma once
#include "config.hh"
#include <logger.h>

namespace detent_logging {
struct Settings {
	int level = LOG_INFO;
	LogColorMode color = LogColorMode::automatic;
};
// Remove logging flags before application-specific argument parsing.
Settings parse(std::vector<std::string>& args, const detent_cli::Config& config);
class Session {
public:
	explicit Session(Settings settings);
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
};
}
