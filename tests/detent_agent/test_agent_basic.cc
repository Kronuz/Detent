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
		"payments:new_processor": { "type": "bool", "default": false, "rollout_pct": 10, "overrides": { "internal_employees": true, "beta": {"rollout_pct":50}, "production": {"rollout_pct":5}, "excluded": false, "all": {"rollout_pct":100}, "none": {"rollout_pct":0} } }
	}
})";

} // namespace

int main() {
	auto truncated = temp_path("_truncated.shm");
	for (auto contents : {"", "short"}) {
		{ std::ofstream out(truncated, std::ios::binary); out << contents; }
		bool rejected = false;
		try { auto reader = detent_agent::ShmReader::open(truncated); }
		catch (const std::runtime_error&) { rejected = true; }
		assert(rejected);
	}
	auto link = temp_path("_link.shm");
	std::filesystem::create_symlink(truncated, link);
	bool rejected_link = false;
	try { auto reader = detent_agent::ShmReader::open(link); }
	catch (const std::runtime_error&) { rejected_link = true; }
	assert(rejected_link);
	std::filesystem::remove(link);
	std::filesystem::remove(truncated);
	assert(!detent_agent::ShmReader::open(truncated));

	// Valid JSON can exceed the separate shared-memory capacity. Startup
	// must fail rather than report ready while publishing no defaults.
	auto oversized_defaults = temp_path("_oversized.json");
	auto oversized_shm = temp_path("_oversized.shm");
	{
		std::ofstream out(oversized_defaults);
		out << R"({"schema_version":1,"knobs":{"test:large":{"type":"bool","default":false,"overrides":{)";
		for (unsigned i = 0; i < 40'000; ++i) {
			if (i) out << ',';
			out << '"' << std::string(120, 'x') << i << R"(":true)";
		}
		out << "}}}}";
	}
	bool rejected_capacity = false;
	try {
		detent_agent::Options oversized;
		oversized.shm_path = oversized_shm;
		oversized.client_options.defaults_path = oversized_defaults;
		detent_agent::Agent invalid(std::move(oversized));
	} catch (const std::runtime_error& e) {
		rejected_capacity = std::string(e.what()).find("shared-memory capacity") != std::string::npos;
	}
	std::filesystem::remove(oversized_defaults);
	std::filesystem::remove(oversized_shm);
	assert(rejected_capacity);

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

	detent::InitOptions comparison_options;
	comparison_options.defaults_path = defaults_path;
	detent::Client comparison(std::move(comparison_options));
	for (auto group : {"beta", "production", "excluded", "all", "none", "unknown", "internal_employees"}) {
		for (int user = 0; user < 1000; ++user) {
			auto hash = "host-" + std::to_string(user);
			auto expected = comparison.eval("payments:new_processor", hash, group).value();
			assert(reader->eval("payments:new_processor", hash, group).value() == expected);
		}
		assert(reader->eval("payments:new_processor", std::nullopt, group).value() ==
		       comparison.eval("payments:new_processor", std::nullopt, group).value());
	}
	assert(!reader->eval("payments:new_processor", "user-1", "production").value());
	assert(reader->eval("payments:new_processor", "user-1", "beta").value());
	assert(!reader->eval("payments:new_processor", std::nullopt, "all").value());

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
