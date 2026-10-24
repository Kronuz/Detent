#include "detent/overrides.hh"
#include "detent/validation.hh"

#include <strict_stox.hh>

#include <cerrno>

namespace detent {

Result<OverrideTable, std::string> parse_cli_overrides(const std::vector<std::string>& raw_pairs) {
	OverrideTable table;
	// Appends in input order and sorts once. Every exit reports a duplicate
	// seen so far first, matching the order of one-by-one insertion.
	flat_map_builder<std::string, OverrideValue, std::less<>> builder(table.values);
	builder.reserve(raw_pairs.size());
	const auto fail = [&](std::string message) -> Result<OverrideTable, std::string> {
		if (auto repeat = builder.finish())
			return std::string("duplicate --just-knob override for: " + builder.entry(*repeat).first);
		return message;
	};

	for (const auto& raw : raw_pairs) {
		auto eq = raw.find('=');
		if (eq == std::string::npos) {
			return fail("malformed --just-knob value (expected name=value): " + raw);
		}
		std::string name = raw.substr(0, eq);
		std::string value = raw.substr(eq + 1);

		if (!is_valid_knob_name(name)) {
			return fail("invalid knob name in override: " + name);
		}

		OverrideValue parsed_value{false};
		bool valid = true;
		if (value == "true") {
			parsed_value = OverrideValue{true};
		} else if (value != "false") {
			int errno_save = 0;
			auto parsed = strict_stoll(&errno_save, value, nullptr, 10);
			valid = errno_save == 0;
			parsed_value = OverrideValue{static_cast<std::int64_t>(parsed)};
		}
		// Pushed before the value check: a duplicate name wins over its own bad value.
		builder.push(name, parsed_value);
		if (!valid) {
			return fail("invalid override value for " + name + ": " + value);
		}
	}

	if (auto repeat = builder.finish())
		return std::string("duplicate --just-knob override for: " + builder.entry(*repeat).first);
	return table;
}

} // namespace detent
