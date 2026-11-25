// Warm lookups in `selected` (quest first-read-1ms, correctness review C2 and R3-1): main's decoded table
// (`StoreTable::find`) and, built with DETENT_PAGED_TABLE, the paged table's verified pages
// (`PagedTable::find`, hot) and a table without marks, whose every lookup checks its page (cold). 1,000
// entries of 16-, 40- and 255-byte names (255 is the format's widest); each case prints one JSON line with
// the best of 7 rounds, present and absent (an absent name falls inside a page).
// `table_find_bench loop WIDTH KIND SECONDS` repeats one case's present lookups, for a profiler.
#include "detent_local/store_table.hh"
#ifdef DETENT_PAGED_TABLE
#include "detent_distribution/storage.hh"
#include <memory>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace l = detent::local;
namespace {
std::string name_of(std::size_t i, std::size_t width) {
	const auto digits = std::to_string(i);
	std::string out = "namespace-";
	return out + std::string(width - out.size() - digits.size(), '0') + digits;
}
template <class F> double ns_per(std::size_t calls, F &&lookup) {
	double best = 1e18;
	for (int round = 0; round < 7; ++round) {
		const auto begin = std::chrono::steady_clock::now();
		std::size_t hits = 0;
		for (std::size_t i = 0; i < calls; ++i)
			hits += lookup(i % 1000);
		const auto end = std::chrono::steady_clock::now();
		asm volatile("" : : "r"(hits) : "memory");
		best = std::min(best, std::chrono::duration<double, std::nano>(end - begin).count() / double(calls));
	}
	return best;
}
void report(const char *kind, std::size_t width, double present, double absent) {
	std::printf("{\"case\": \"%s/%zu\", \"present_ns\": %.1f, \"absent_ns\": %.1f}\n", kind, width, present, absent);
}
struct Table {
	std::string bytes;
	l::StoreTable decoded;
	std::vector<std::string> present, absent;
};
Table table_of(std::size_t width) {
	Table out;
	l::StoreTable table;
	for (std::size_t i = 0; i < 1000; ++i) {
		l::TableEntry entry;
		entry.name = name_of(i * 2, width); // odd numbers are absent
		entry.slot = static_cast<std::uint32_t>(i);
		entry.generation[0] = 1;
		entry.root_length = 320;
		table.entries.push_back(entry);
	}
	for (std::size_t i = 0; i < 1000; ++i) {
		out.present.push_back(name_of((i * 7919) % 1000 * 2, width));
		out.absent.push_back(name_of((i * 7919) % 1000 * 2 + 1, width));
	}
	out.bytes = l::encode_table(table);
	out.decoded = l::decode_table(std::span<const char>(out.bytes.data(), out.bytes.size()));
	return out;
}
} // namespace

int main(int argc, char **argv) {
	if (argc == 5 && std::string_view(argv[1]) == "loop") {
		const auto t = table_of(std::strtoul(argv[2], nullptr, 10));
		const std::string_view kind = argv[3];
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(std::atoi(argv[4]));
#ifdef DETENT_PAGED_TABLE
		const auto account = std::make_shared<detent::distribution::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(std::vector<char>(t.bytes.begin(), t.bytes.end()), account);
#endif
		std::size_t hits = 0;
		while (std::chrono::steady_clock::now() < until)
			for (const auto &name : t.present) {
#ifdef DETENT_PAGED_TABLE
				if (kind == "paged")
					hits += bool(paged.find(name));
				else
#endif
					hits += t.decoded.find(name) != nullptr;
			}
		std::printf("%zu\n", hits);
		return 0;
	}
	for (const std::size_t width : {16, 40, 255}) {
		const auto t = table_of(width);
		report("decoded", width, ns_per(200000, [&](std::size_t i) { return t.decoded.find(t.present[i]) != nullptr; }),
		       ns_per(200000, [&](std::size_t i) { return t.decoded.find(t.absent[i]) != nullptr; }));
#ifdef DETENT_PAGED_TABLE
		const auto account = std::make_shared<detent::distribution::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(std::vector<char>(t.bytes.begin(), t.bytes.end()), account);
		for (const auto &name : t.present)
			(void)paged.find(name); // every page verified and marked
		report("paged", width, ns_per(200000, [&](std::size_t i) { return bool(paged.find(t.present[i])); }),
		       ns_per(200000, [&](std::size_t i) { return bool(paged.find(t.absent[i])); }));
		const l::PagedTable cold(std::vector<char>(t.bytes.begin(), t.bytes.end()));
		report("cold", width, ns_per(2000, [&](std::size_t i) { return bool(cold.find(t.present[i])); }),
		       ns_per(2000, [&](std::size_t i) { return bool(cold.find(t.absent[i])); }));
#endif
	}
	return 0;
}
