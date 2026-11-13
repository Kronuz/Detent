// The agent store's collection (AgentStore::collect through DiskStore): a repair supersedes a
// `count`-record generation and shares none of its packs, so one collection removes that generation
// and every pack; a second collection finds nothing to remove (its fixed cost).
#include "detent_testing/generation_writer.hh"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
using Clock = std::chrono::steady_clock;
namespace {
void check_at(bool value, unsigned line) {
	if (!value)
		throw std::runtime_error("collection benchmark invariant failed at line " + std::to_string(line));
}
#define check(value) check_at((value), __LINE__)
std::string corpus(unsigned long count) {
	std::string out = R"({"schema_version":1,"knobs":{)";
	for (unsigned long i = 0; i < count; ++i)
		out += (i ? ",\"app:k" : "\"app:k") + std::to_string(i) + R"(":{"default":)" + std::to_string(i) + "}";
	return out + "}}";
}
struct Census {
	std::uint64_t files = 0, bytes = 0, packs = 0;
};
Census census(const std::filesystem::path &directory) {
	Census out;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		++out.files;
		out.bytes += entry.file_size();
		out.packs += entry.path().extension() == ".pack";
	}
	return out;
}
} // namespace
int main(int argc, char **argv) {
	const char *stage = "setup";
	try {
		const auto count = argc > 1 ? std::stoul(argv[1]) : 100000;
		if (argc > 2 || count == 0 || count > 500000)
			throw std::invalid_argument("expected count 1..500000");
		const auto directory = std::filesystem::current_path() / ".scratch" /
							   ("paged-collection-bench-" + std::to_string(::getpid()));
		struct Cleanup {
			std::filesystem::path path;
			~Cleanup() {
				std::error_code error;
				std::filesystem::remove_all(path, error);
			}
		} cleanup{directory};
		const auto json = corpus(count);
		detent::testing::GenerationWriter writer(directory);
		stage = "publication";
		const auto revision = writer.publish_json(json);
		stage = "repair";
		check(writer.repair_json(json) == revision);
		for (const std::string_view run : {"superseded", "idle"}) {
			stage = run.data();
			const auto before = census(directory);
			const auto start = Clock::now();
			writer.collect();
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			const auto after = census(directory);
			check((run == "idle") == (after.files == before.files) && after.files <= before.files);
			std::cout << "{\"records\":" << count << ",\"run\":\"" << run << "\",\"files_before\":" << before.files
					  << ",\"packs_before\":" << before.packs << ",\"bytes_before\":" << before.bytes
					  << ",\"files_removed\":" << before.files - after.files
					  << ",\"packs_removed\":" << before.packs - after.packs
					  << ",\"bytes_removed\":" << before.bytes - after.bytes << ",\"wall_us\":" << elapsed << "}\n";
		}
	} catch (const std::exception &error) {
		std::cerr << stage << ": " << error.what() << '\n';
		return 1;
	}
}
