#pragma once
// Exploration-only switches (explore/agent-startup); never for main.
#include <cstdlib>
namespace detent::local::detail {
inline bool explore_env(const char *name) noexcept {
	const char *value = std::getenv(name);
	return value && *value == '1';
}
inline const bool explore_skip_startup_sync = explore_env("DETENT_EXPLORE_SKIP_STARTUP_SYNC");
inline const bool explore_skip_rehash = explore_env("DETENT_EXPLORE_SKIP_REHASH");
inline const bool explore_skip_allhash = explore_env("DETENT_EXPLORE_SKIP_ALLHASH");
} // namespace detent::local::detail
