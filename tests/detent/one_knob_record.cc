// Records main's answer for every A1 cell (one_knob_cells.hh) as one bit per cell, in
// enumeration order, as the hex lines of one_knob_main.hex. Built only against main's tree by
// one_knob_record.sh; the differential test reads the file.
#include <cstdio>
#include <stdexcept>
#include <string>

#include "detent/client.hh"
#include "detent/json_schema.hh"
#include "one_knob_cells.hh"

int main() try {
	using namespace one_knob_cells;
	std::string bits;
	for (const auto &definition : definitions()) {
		auto snapshot = detent::parse_defaults(main_json(definition));
		if (!snapshot) throw std::runtime_error("main refused " + main_json(definition));
		detent::Client client(std::move(snapshot).value());
		for (const auto &group : groups())
			for (const auto &hash : hashes()) {
				std::optional<std::string_view> g, h;
				if (group) g = *group;
				if (hash) h = *hash;
				const auto value = client.eval(kName, h, g);
				if (!value) throw std::runtime_error("main read failed");
				bits += value.value() ? '1' : '0';
			}
	}
	std::printf("# cells %zu\n", bits.size());
	while (bits.size() % 256) bits += '0';
	for (std::size_t line = 0; line != bits.size(); line += 256) {
		for (std::size_t at = line; at != line + 256; at += 4)
			std::putchar("0123456789abcdef"[(bits[at] - '0') << 3 | (bits[at + 1] - '0') << 2 |
											(bits[at + 2] - '0') << 1 | (bits[at + 3] - '0')]);
		std::putchar('\n');
	}
	return 0;
} catch (const std::exception &error) {
	std::fprintf(stderr, "one_knob_record: %s\n", error.what());
	return 1;
}
