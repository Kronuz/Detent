// Worked example: init from a baked-defaults file plus a live file source,
// evaluate a kill switch and an int knob, then print a health snapshot.
// Build via CMake: cmake -B build && cmake --build build && ./build/examples/detent_demo
#include "detent/client.hh"

#include <cstdio>
#include <fstream>

int main() {
	auto defaults_path = std::filesystem::temp_directory_path() / "detent_demo_defaults.json";
	{
		std::ofstream out(defaults_path, std::ios::binary);
		out << R"({
			"schema_version": 1,
			"knobs": {
				"checkout:killswitch_new_path": { "type": "bool", "default": false },
				"search:max_results": { "type": "int", "default": 25 }
			}
		})";
	}

	detent::InitOptions opts;
	opts.defaults_path = defaults_path;
	// No live source configured in this demo -- reads run entirely off the
	// version-0 baked defaults.

	detent::Client client(std::move(opts));

	bool killswitch = client.eval_or_panic("checkout:killswitch_new_path");
	std::int64_t max_results = client.get_or_panic("search:max_results");
	std::printf("killswitch_new_path = %s\n", killswitch ? "true" : "false");
	std::printf("max_results = %lld\n", static_cast<long long>(max_results));

	auto health = client.health();
	std::printf(
		"health: initialized=%d source_connected=%d accepted_version=%llu "
		"bool_knobs=%zu int_knobs=%zu cli_overrides=%zu\n",
		health.initialized, health.source_connected,
		static_cast<unsigned long long>(health.accepted_version),
		health.bool_knobs, health.int_knobs, health.cli_overrides);

	std::filesystem::remove(defaults_path);
	return 0;
}
