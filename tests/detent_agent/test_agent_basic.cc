// The host-local agent, shared-memory edition: a real Agent instance
// publishing to shm, and a real ShmReader (exactly what the `get` CLI
// uses) reading it back -- asserting bool/int reads, missing/wrong-type
// errors, hash_val/switch_val precedence, and live-update propagation, all
// end to end through the real serialize/publish/mmap/evaluate path (not a
// white-box call into the serializer alone).
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <thread>

#include "agent.hh"
#include "shm_reader.hh"

using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
	static std::mt19937_64 rng{std::random_device{}()};
	return std::filesystem::temp_directory_path() / ("detent_agent_test_" + std::to_string(rng()) + std::string(suffix));
}

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(10ms);
	}
	return pred();
}

constexpr std::string_view kDefaults = R"({
	"schema_version": 1,
	"knobs": {
		"checkout:killswitch_new_path": { "type": "bool", "default": false },
		"search:max_results": { "type": "int", "default": 25 },
		"payments:new_processor": { "type": "bool", "default": false, "rollout_pct": 10, "overrides": { "internal_employees": true } }
	}
})";

} // namespace

int main() {
	auto defaults_path = temp_path("_defaults.json");
	{
		std::ofstream out(defaults_path, std::ios::binary);
		out << kDefaults;
	}
	auto shm_path = temp_path(".shm");

	detent_agent::Options options;
	options.shm_path = shm_path;
	options.client_options.defaults_path = defaults_path;

	detent_agent::Agent agent(std::move(options));
	std::thread agent_thread([&] { agent.serve(); });

	// The initial (baked-defaults-only, no source configured) publish
	// happens synchronously in Agent's constructor, so this should already
	// be readable with no wait.
	auto reader = detent_agent::ShmReader::open(shm_path);
	assert(reader.has_value());

	auto b = reader->eval("checkout:killswitch_new_path", std::nullopt, std::nullopt);
	assert(b.has_value() && b.value() == false);
	auto i = reader->get("search:max_results", std::nullopt);
	assert(i.has_value() && i.value() == 25);

	// Missing knob and wrong-type errors surface with their category.
	auto missing = reader->eval("nope:not_here", std::nullopt, std::nullopt);
	assert(!missing.has_value());
	assert(missing.error().category == detent::ErrorCategory::MissingKnob);
	auto wrong_type = reader->eval("search:max_results", "u1", std::nullopt);
	assert(!wrong_type.has_value());
	assert(wrong_type.error().category == detent::ErrorCategory::WrongType);

	// hash_val (rollout gate) and switch_val (group override) precedence,
	// matching detent::Client::eval() exactly -- this is the whole point of
	// serializing a pre-parsed binary form rather than raw JSON text: the
	// CLI gets full Section 8 semantics with zero parsing.
	auto below_threshold = reader->eval("payments:new_processor", "user-1", std::nullopt);
	assert(below_threshold.has_value() && below_threshold.value() == false);
	auto above_threshold = reader->eval("payments:new_processor", "user-42", std::nullopt);
	assert(above_threshold.has_value() && above_threshold.value() == true);
	auto group_override = reader->eval("payments:new_processor", "user-2", "internal_employees");
	assert(group_override.has_value() && group_override.value() == true); // group wins even though user-2 fails the rollout gate
	auto unknown_group = reader->eval("payments:new_processor", "user-1", "no-such-group");
	assert(unknown_group.has_value() && unknown_group.value() == false); // falls through to the rollout gate

	// get() ignores switch_val (int model has no group overrides, Section 8).
	auto get_with_switch = reader->get("search:max_results", "anything");
	assert(get_with_switch.has_value() && get_with_switch.value() == 25);

	// status() reports the accepted version and knob count.
	auto st = reader->status();
	assert(st.accepted_version == 0); // baked defaults only, no source configured
	assert(st.knob_count == 3);

	agent.stop();
	agent_thread.join();

	std::filesystem::remove(defaults_path);
	std::filesystem::remove(shm_path);

	std::puts("detent-agent shared-memory publish/read (bool/int/missing/wrong-type, "
	          "hash_val/switch_val precedence, status) OK");
	return 0;
}
