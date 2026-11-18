// A1: every former bool knob, lifted to one int64 definition (default 0/1, rollout value 1),
// answers through Client::get exactly what main's evaluator answered, cell by cell, against
// main's answers recorded in one_knob_main.hex (one_knob_record.sh). The one deliberate change
// (D68) is enumerated: a default-true knob's hashed caller outside the applied percentage, where
// main answered false, now gets the default 1.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "detent/client.hh"
#include "detent/hashing.hh"
#include "detent/json_schema.hh"
#include "one_knob_cells.hh"

namespace {

void check(bool value, int line) {
	if (!value) {
		std::fprintf(stderr, "differential invariant failed at line %d\n", line);
		std::abort();
	}
}
#define CHECK(value) check(value, __LINE__)

// main's recorded bits, in enumeration order.
std::string recorded(std::size_t &cells) {
	std::ifstream file(DETENT_ONE_KNOB_MAIN);
	CHECK(file.good());
	std::string line, bits;
	cells = 0;
	while (std::getline(file, line)) {
		if (line.rfind("# cells ", 0) == 0) cells = std::stoul(line.substr(8));
		if (line.empty() || line[0] == '#') continue;
		for (char c : line) {
			const int nibble = c <= '9' ? c - '0' : c - 'a' + 10;
			CHECK(nibble >= 0 && nibble < 16);
			for (int bit = 3; bit >= 0; --bit) bits += (nibble >> bit & 1) ? '1' : '0';
		}
	}
	CHECK(cells && bits.size() >= cells);
	return bits;
}

} // namespace

int main() {
	using namespace one_knob_cells;
	std::size_t cells = 0;
	const auto main_bits = recorded(cells);
	std::size_t at = 0;
	unsigned long equal = 0, changed = 0;
	for (const auto &definition : definitions()) {
		auto snapshot = detent::parse_defaults(one_knob_json(definition));
		CHECK(snapshot.has_value());
		detent::Client client(std::move(snapshot).value());
		for (const auto &group : groups())
			for (const auto &hash : hashes()) {
				std::optional<std::string_view> g, h;
				if (group) g = *group;
				if (hash) h = *hash;
				CHECK(at < cells);
				const bool before = main_bits[at++] == '1';
				const auto after = client.get(kName, h, g);
				CHECK(after.has_value());
				const auto pct = applied_percentage(definition, group);
				if (definition.fallback && pct && hash && detent::stable_bucket(kName, *hash) >= *pct) {
					CHECK(!before && after.value() == 1);
					++changed;
				} else {
					CHECK(after.value() == (before ? 1 : 0));
					++equal;
				}
			}
	}
	CHECK(at == cells);
	std::printf("%lu equal cells, %lu changed cells (default-true, hashed, outside the percentage)\n", equal,
				changed);
	CHECK(equal > 0 && changed > 0);
	return 0;
}
