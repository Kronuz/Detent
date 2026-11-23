// Reads every key of an N-record store through the reader core at the default mapping cap and at a
// raised one, and through the prepared client view at main's default ViewLimits (reader-core.md,
// Bounds). Exits 1 unless the raised cap reads every key and the default answers as EXPECT says:
// "reads" every key, or "refuses" some with DETENT_NO_MEMORY alone.
// usage: reader_capacity_probe N RAISED_BYTES EXPECT
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include "prepared_reader.hh"
#include "testing.hh"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>

namespace x = detent::local::detail;
namespace t = detent::reader::testing;
namespace {
std::string name(std::size_t i) { return "n" + std::to_string(i % 64) + ":i" + std::to_string(i); }
struct Read {
	std::map<std::string, std::size_t> statuses;
	std::size_t ok = 0, wrong = 0;
};
Read read_all(const std::string &path, std::size_t n, std::uint64_t mapping_bytes) {
	detent_open_options options{sizeof options, {path.data(), path.size()}, nullptr, 0, uint32_t(::getuid()),
	                            mapping_bytes};
	detent_reader *h = nullptr;
	if (detent_open(&options, &h) != DETENT_OK)
		throw std::runtime_error("open");
	Read read;
	std::size_t first_refused = SIZE_MAX;
	for (std::size_t i = 0; i != n; ++i) {
		const auto key = name(i);
		std::int64_t out = -7;
		const auto status = detent_get(h, {key.data(), key.size()}, {}, {}, -7, &out);
		++read.statuses[detent_status_name(status)];
		if (status != DETENT_OK && first_refused == SIZE_MAX)
			first_refused = i;
		read.ok += status == DETENT_OK && out == std::int64_t(i);
		read.wrong += status == DETENT_OK ? out != std::int64_t(i) : out != -7;
	}
	const auto stats = t::stats(h);
	detent_close(h);
	std::cout << "  cap " << stats.mapped_limit << ": charged " << stats.mapped_bytes << " ("
	          << double(stats.mapped_bytes) / double(n) << " per record), wrong values " << read.wrong;
	for (const auto &[status, count] : read.statuses)
		std::cout << ", " << status << " " << count;
	if (first_refused != SIZE_MAX)
		std::cout << ", first refused key index " << first_refused;
	std::cout << "\n";
	return read;
}
} // namespace
int main(int argc, char **argv) {
	if (argc != 4 || (std::string(argv[3]) != "reads" && std::string(argv[3]) != "refuses")) {
		std::cerr << "usage: reader_capacity_probe N RAISED_BYTES reads|refuses\n";
		return 2;
	}
	const std::size_t n = std::stoull(argv[1]);
	const std::uint64_t raised = std::stoull(argv[2]);
	const bool refuses = std::string(argv[3]) == "refuses";
	const auto root = std::filesystem::current_path() / ".scratch" / ("capacity-probe-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	const auto store = root / "store";
	const auto start = std::chrono::steady_clock::now();
	{
		detent::testing::GenerationWriter writer(store);
		std::string json = R"({"schema_version":1,"knobs":{)";
		for (std::size_t i = 0; i != n; ++i)
			json += (i ? ",\"" : "\"") + name(i) + R"(":{"default":)" + std::to_string(i) + "}";
		writer.publish_json(json + "}}");
	}
	std::uintmax_t packs = 0, pack_bytes = 0;
	for (const auto &entry : std::filesystem::directory_iterator(store))
		if (entry.path().extension() == ".pack")
			++packs, pack_bytes += entry.file_size();
	std::cout << "records " << n << ", packs " << packs << ", pack bytes " << pack_bytes << ", built in "
	          << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count()
	          << " s\n";
	const auto path = store.string();
	std::cout << "reader core, raised cap:\n";
	const auto wide = read_all(path, n, raised);
	std::cout << "reader core, default cap:\n";
	const auto capped = read_all(path, n, 0);
	std::cout << "prepared client view, default ViewLimits (main's client path):\n";
	auto client = x::PreparedReader::open(store, ::getuid(), {});
	std::size_t ok = 0, first = SIZE_MAX;
	for (std::size_t i = 0; i != n; ++i) {
		const auto key = name(i);
		const auto colon = key.find(':');
		const auto result =
		    client.view.get(std::string_view(key).substr(0, colon), std::string_view(key).substr(colon + 1));
		if (result && result.value() == std::int64_t(i))
			++ok;
		else if (first == SIZE_MAX)
			first = i;
	}
	std::cout << "  ok " << ok << ", refused " << n - ok;
	if (first != SIZE_MAX)
		std::cout << ", first refused key index " << first;
	std::cout << "\n";
	const bool raised_reads = wide.ok == n;
	const auto no_memory = capped.statuses.find("no_memory");
	const std::size_t refused = no_memory == capped.statuses.end() ? 0 : no_memory->second;
	const bool default_as_expected =
	    capped.wrong == 0 && (refuses ? capped.ok < n && capped.ok + refused == n : capped.ok == n);
	std::cout << "raised cap reads every key: " << (raised_reads ? "yes" : "NO") << "; default cap "
	          << (refuses ? "refuses with no_memory alone" : "reads every key") << ": "
	          << (default_as_expected ? "yes" : "NO") << "\n";
	return raised_reads && default_as_expected ? 0 : 1;
}
