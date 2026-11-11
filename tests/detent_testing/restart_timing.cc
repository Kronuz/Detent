// Restart timing for namespace step S3: writes a store of N int knobs through the agent's own publication,
// then times the agent's cold start (storage census and recovery to Ready) over APFS clones of it.
//   restart_timing publish DIR N
//   restart_timing restart DIR RUNS
//   restart_timing q2pub DIR RUNS     interleaved forward snapshot and repair publications of 100,000 knobs, with
//                                      the page digests each one's recovery hashes (every snapshot recovers cold)
#include "detent_local/paged_layout.hh"
#include "detent_testing/generation_writer.hh"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
int main(int argc, char **argv) {
	if (argc != 4) {
		std::cerr << "usage: restart_timing publish DIR N | restart DIR RUNS | q2pub DIR RUNS\n";
		return 2;
	}
	const std::string mode = argv[1];
	const fs::path dir = argv[2];
	const int count = std::atoi(argv[3]);
	if (mode == "q2pub") {
		auto json = [](int revision) {
			std::string out = R"({"schema_version":1,"knobs":{)";
			for (int i = 0; i < 100000; ++i)
				out += (i ? "," : "") + std::string("\"n") + std::to_string(i % 10) + ":k" + std::to_string(i) +
					   R"(":{"type":"int","default":)" + std::to_string(std::int64_t(i) * 3 + revision) + "}";
			return out + "}}";
		};
		fs::remove_all(dir);
		detent::testing::GenerationWriter writer(dir);
		writer.publish_json(json(1));
		std::vector<double> forward, repair;
		auto timed = [&](auto publish, const char *kind, std::vector<double> &times) {
			const auto document = json(int(writer.revision()) + (kind[0] == 'f'));
			const auto digests = detent::local::paged::hash_counts().page_calls;
			const auto start = std::chrono::steady_clock::now();
			publish(document);
			const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
			times.push_back(elapsed.count());
			std::cout << kind << " revision " << writer.revision() << ": " << elapsed.count() << " ms, "
					  << detent::local::paged::hash_counts().page_calls - digests << " page digests\n";
			writer.collect();
		};
		for (int run = 0; run < count; ++run) {
			timed([&](const std::string &document) { writer.publish_json(document); }, "forward", forward);
			timed([&](const std::string &document) { writer.repair_json(document); }, "repair", repair);
		}
		auto median = [](std::vector<double> times) {
			std::sort(times.begin(), times.end());
			return times[times.size() / 2];
		};
		std::cout << "q2pub: forward median " << median(forward) << " ms, repair median " << median(repair)
				  << " ms over " << count << " runs each\n";
		fs::remove_all(dir);
		return 0;
	}
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
