#pragma once
#include "detent_delivery/client.hh"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace detent::fleet {
struct Options {
	std::vector<delivery::ClientOptions> sources;
	std::filesystem::path result;
	std::size_t subscribers = 500, active = 128, workers = 1;
	std::size_t heap_bytes = 512ull * 1024 * 1024;
	std::chrono::seconds lifetime{660};
	std::uint64_t seed = 17;
	std::optional<std::uint64_t> warm_revision;
	std::optional<std::size_t> fail_launch_after;
};
// One bounded stdin checkpoint follows writer reconciliation. Probe feedback
// records transport consumption; independently managed stores prove durability.
int run(Options);
} // namespace detent::fleet
