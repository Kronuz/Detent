// Worked example: open a reader over a managed agent's store, read a kill switch and an int knob with
// per-call fallbacks, and show what a lookup reports when a knob is unavailable.
//
//   cmake -B build && cmake --build build && ./build/examples/detent_demo STORE_DIR AGENT_UID
//
// STORE_DIR is the agent's generation directory (detent-agent --generation-dir), AGENT_UID the uid the
// agent runs as: trust is never inferred from the directory's owner. The reader never raises on a lookup;
// it returns the fallback you pass, and lookup() also says why.
#include "detent/reader.hh"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: %s STORE_DIR AGENT_UID\n", argv[0]);
		return 2;
	}
	try {
		detent::Reader reader(argv[1], static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)));

		bool killswitch = reader.eval("checkout:killswitch_new_path", false);
		std::int64_t max_results = reader.get("search:max_results", 25);
		std::printf("killswitch_new_path = %s\n", killswitch ? "true" : "false");
		std::printf("max_results = %lld\n", static_cast<long long>(max_results));

		// The status behind a fallback: missing knob, wrong type, no generation yet, and so on.
		const auto result = reader.lookup("search:max_results");
		std::printf("lookup: %s\n", detent_status_name(result.status));
		if (const auto revision = reader.revision("search"))
			std::printf("search revision = %llu\n", static_cast<unsigned long long>(*revision));
	} catch (const detent::ReaderError &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
	return 0;
}
