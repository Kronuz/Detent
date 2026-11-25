// Warm lookups in `selected` (quest first-read-1ms, correctness review C2): main's decoded table
// (`StoreTable::find`) and, built with DETENT_PAGED_TABLE, the paged table's verified pages
// (`PagedTable::find`). 1,000 entries of 16- and 40-byte names; each case prints one JSON line with
// the best of 7 rounds of 200,000 lookups, present and absent (an absent name falls inside a page).
#include "detent_local/store_table.hh"
#ifdef DETENT_PAGED_TABLE
#include "detent_distribution/storage.hh"
#include <memory>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace l = detent::local;
namespace {
std::string name_of(std::size_t i, std::size_t width) {
	const auto digits = std::to_string(i);
	std::string out = "namespace-";
	return out + std::string(width - out.size() - digits.size(), '0') + digits;
}
template <class F> double ns_per(F &&lookup) {
	constexpr std::size_t calls = 200000;
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
} // namespace

int main() {
	for (const std::size_t width : {16, 40}) {
		l::StoreTable table;
		for (std::size_t i = 0; i < 1000; ++i) {
			l::TableEntry entry;
			entry.name = name_of(i * 2, width); // odd numbers are absent
			entry.slot = static_cast<std::uint32_t>(i);
			entry.generation[0] = 1;
			entry.root_length = 320;
			table.entries.push_back(entry);
		}
		std::vector<std::string> present, absent;
		for (std::size_t i = 0; i < 1000; ++i) {
			present.push_back(name_of((i * 7919) % 1000 * 2, width));
			absent.push_back(name_of((i * 7919) % 1000 * 2 + 1, width));
		}
		const auto bytes = l::encode_table(table);
		const auto decoded = l::decode_table(std::span<const char>(bytes.data(), bytes.size()));
		report("decoded", width, ns_per([&](std::size_t i) { return decoded.find(present[i]) != nullptr; }),
		       ns_per([&](std::size_t i) { return decoded.find(absent[i]) != nullptr; }));
#ifdef DETENT_PAGED_TABLE
		const auto account = std::make_shared<detent::distribution::StorageLedger>(std::size_t(1) << 30);
		const l::PagedTable paged(std::vector<char>(bytes.begin(), bytes.end()), account);
		for (const auto &name : present)
			(void)paged.find(name); // every page verified and marked
		report("paged", width, ns_per([&](std::size_t i) { return bool(paged.find(present[i])); }),
		       ns_per([&](std::size_t i) { return bool(paged.find(absent[i])); }));
#endif
	}
	return 0;
}
