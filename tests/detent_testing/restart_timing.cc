// Restart timing for namespace step S3: writes a store of N int knobs through the agent's own publication,
// then times the agent's cold start (storage census and recovery to Ready) over APFS clones of it.
//   restart_timing publish DIR N
//   restart_timing restart DIR RUNS
#include "detent_testing/generation_writer.hh"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
int main(int argc, char **argv) {
	if (argc != 4) {
		std::cerr << "usage: restart_timing publish DIR N | restart DIR RUNS\n";
		return 2;
	}
	const std::string mode = argv[1];
	const fs::path dir = argv[2];
	const int count = std::atoi(argv[3]);
	if (mode == "publish") {
		std::string json = R"({"schema_version":1,"knobs":{)";
		for (int i = 0; i < count; ++i)
			json += (i ? "," : "") + std::string("\"n") + std::to_string(i % 10) + ":k" + std::to_string(i) +
					R"(":{"type":"int","default":)" + std::to_string(std::int64_t(i) * 3 + 1) + "}";
		json += "}}";
		detent::testing::GenerationWriter writer(dir);
		writer.publish_json(json);
		std::cout << "published " << count << " knobs at revision " << writer.revision() << "\n";
		return 0;
	}
	for (int run = 0; run < count; ++run) {
		const auto clone = dir.parent_path() / (dir.filename().string() + ".clone");
		fs::remove_all(clone);
		const auto command = "cp -cR '" + dir.string() + "' '" + clone.string() + "'";
		if (std::system(command.c_str()) != 0)
			return 3;
		const auto start = std::chrono::steady_clock::now();
		{
			detent::testing::GenerationWriter writer(clone);
			const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
			std::cout << "restart " << dir.filename().string() << " run " << run << ": " << elapsed.count()
					  << " s (revision " << writer.revision() << ")\n";
		}
		fs::remove_all(clone);
	}
	return 0;
}
