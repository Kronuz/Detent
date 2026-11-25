// Warm lookups in `selected` (quest first-read-1ms, correctness review C2, R3-1 and R4-1): main's decoded
// table (`StoreTable::find`) and, built with DETENT_PAGED_TABLE, the paged table's verified pages
// (`PagedTable::find`, hot) and a table without marks, whose every lookup checks its page (cold). 1,000
// entries a table, one table a shape:
//   prefix-W   "namespace-" and a zero-padded number, W bytes (16, 40, 255; 255 is the format's widest)
//   random-W   W random bytes of [a-z0-9] (4, 8, 16, 40): neighbours share about one byte, no table prefix
// Absent names are names of the same shape not in the table, so they fall inside pages. Each case prints one
// JSON line with the best of 7 rounds, present and absent.
// `table_find_bench loop SHAPE KIND SECONDS` repeats one case's present lookups, for a profiler.
#include "detent_local/store_table.hh"
#ifdef DETENT_PAGED_TABLE
#include "detent_distribution/storage.hh"
#include <memory>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace l = detent::local;
namespace {
constexpr std::string_view shapes[] = {"prefix-16", "prefix-40", "prefix-255", "random-4",
                                       "random-8",  "random-16", "random-40"};

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
void report(const char *kind, std::string_view shape, double present, double absent) {
	std::printf("{\"case\": \"%s/%.*s\", \"present_ns\": %.1f, \"absent_ns\": %.1f}\n", kind, int(shape.size()),
	            shape.data(), present, absent);
}
std::string prefixed(std::size_t i, std::size_t width) {
	const auto digits = std::to_string(i);
	std::string out = "namespace-";
	return out + std::string(width - out.size() - digits.size(), '0') + digits;
}
std::string random_name(std::mt19937_64 &rng, std::size_t width) {
	static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	std::string name;
	while (name.size() < width)
		name.push_back(alphabet[rng() % 36]);
	return name;
}
struct Table {
	std::string bytes;
	l::StoreTable decoded;
	std::vector<std::string> present, absent;
};
// Sorted present and absent names of one shape, 1,000 each.
std::pair<std::vector<std::string>, std::vector<std::string>> names_of(std::string_view shape) {
	const auto dash = shape.find('-');
	const auto width = std::strtoul(shape.substr(dash + 1).data(), nullptr, 10);
	std::vector<std::string> present, absent;
	if (shape.starts_with("prefix")) {
		for (std::size_t i = 0; i < 1000; ++i) {
			present.push_back(prefixed(i * 2, width)); // odd numbers are absent
			absent.push_back(prefixed(i * 2 + 1, width));
		}
		return {present, absent};
	}
	std::mt19937_64 rng(width);
	std::set<std::string> names, others;
	while (names.size() < 1000)
		names.insert(random_name(rng, width));
	while (others.size() < 1000)
		if (auto name = random_name(rng, width); !names.contains(name))
			others.insert(std::move(name));
	return {{names.begin(), names.end()}, {others.begin(), others.end()}};
}
Table table_of(std::string_view shape) {
	const auto [present, absent] = names_of(shape);
	Table out;
	l::StoreTable table;
	for (std::size_t i = 0; i < present.size(); ++i) {
		l::TableEntry entry;
		entry.name = present[i];
		entry.slot = static_cast<std::uint32_t>(i);
		entry.generation[0] = 1;
		entry.root_length = 320;
		table.entries.push_back(entry);
	}
	for (std::size_t i = 0; i < 1000; ++i) {
		out.present.push_back(present[(i * 7919) % 1000]);
		out.absent.push_back(absent[(i * 7919) % 1000]);
	}
	out.bytes = l::encode_table(table);
	out.decoded = l::decode_table(std::span<const char>(out.bytes.data(), out.bytes.size()));
	return out;
}
} // namespace

int main(int argc, char **argv) {
	if (argc == 5 && std::string_view(argv[1]) == "loop") {
		const auto t = table_of(argv[2]);
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
	for (const auto shape : shapes) {
		const auto t = table_of(shape);
		report("decoded", shape, ns_per(200000, [&](std::size_t i) { return t.decoded.find(t.present[i]) != nullptr; }),
		       ns_per(200000, [&](std::size_t i) { return t.decoded.find(t.absent[i]) != nullptr; }));
#ifdef DETENT_PAGED_TABLE
		const auto account = std::make_shared<detent::distribution::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(std::vector<char>(t.bytes.begin(), t.bytes.end()), account);
		for (const auto &name : t.present)
			(void)paged.find(name); // every page verified and marked
		report("paged", shape, ns_per(200000, [&](std::size_t i) { return bool(paged.find(t.present[i])); }),
		       ns_per(200000, [&](std::size_t i) { return bool(paged.find(t.absent[i])); }));
		const l::PagedTable cold(std::vector<char>(t.bytes.begin(), t.bytes.end()));
		report("cold", shape, ns_per(2000, [&](std::size_t i) { return bool(cold.find(t.present[i])); }),
		       ns_per(2000, [&](std::size_t i) { return bool(cold.find(t.absent[i])); }));
#endif
	}
	return 0;
}
