// detent_update_bench -- throughput of the update pipeline's actual CPU
// cost: parsing + strict-schema-validating a source snapshot and merging it
// over baked defaults (Section 4's "validates off-path... merges over
// code-versioned baked defaults" -- this is that work, isolated from the
// real-time poll_interval/backoff loop that normally paces it). The atomic
// publish step this feeds is not separately benchmarked here: it is a single
// SnapshotRef store, effectively free next to a JSON parse (see
// detent_read_bench for the atomic-load side of that same primitive).
//
//   cmake --build build && ./build/detent_update_bench [iterations] [num-knobs]

#include "detent/json_schema.hh"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using Clock = std::chrono::steady_clock;

namespace {

// Builds a `num_knobs`-entry source snapshot and matching baked-defaults
// payload -- a synthetic but realistically-shaped deployment (mixed bool/int
// knobs, some with rollout percentages, namespaced names).
std::pair<std::string, std::string> build_payloads(std::size_t num_knobs) {
	std::ostringstream defaults, live;
	defaults << R"({"schema_version":1,"knobs":{)";
	live << R"({"schema_version":1,"version":2,"knobs":{)";
	for (std::size_t i = 0; i < num_knobs; ++i) {
		if (i) { defaults << ','; live << ','; }
		auto name = "ns" + std::to_string(i % 20) + ":knob_" + std::to_string(i);
		if (i % 2 == 0) {
			defaults << '"' << name << R"(":{"type":"bool","default":false})";
			live << '"' << name << R"(":{"type":"bool","default":false,"rollout_pct":)" << (i % 101) << "}";
		} else {
			defaults << '"' << name << R"(":{"type":"int","default":)" << i << "}";
			live << '"' << name << R"(":{"type":"int","default":)" << (i * 2) << "}";
		}
	}
	defaults << "}}";
	live << "}}";
	return {defaults.str(), live.str()};
}

} // namespace

int main(int argc, char** argv) {
	std::uint64_t iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200'000;
	std::size_t num_knobs = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 100;

	auto [defaults_json, live_json] = build_payloads(num_knobs);
	auto defaults = detent::parse_defaults(defaults_json);
	if (!defaults) {
		std::fprintf(stderr, "detent_update_bench: bad synthetic defaults payload\n");
		return 1;
	}

	std::uint64_t ok = 0;
	auto t0 = Clock::now();
	for (std::uint64_t i = 0; i < iterations; ++i) {
		auto parsed = detent::parse_source_snapshot(live_json, "bench", num_knobs + 1);
		if (parsed) {
			auto merged = detent::merge_over_defaults(defaults.value(), parsed.value());
			ok += merged.knobs.size() == num_knobs;
		}
	}
	auto elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

	std::printf(
		"detent_update_bench: %llu updates x %zu knobs, %.0f updates/sec (%.1f us/update), %llu succeeded\n",
		(unsigned long long)iterations, num_knobs, iterations / elapsed, elapsed * 1e6 / iterations,
		(unsigned long long)ok);
	return ok == iterations ? 0 : 1;
}
