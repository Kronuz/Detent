#include "detent/overrides.hh"
#include "detent/validation.hh"

#include <strict_stox.hh>

#include <cerrno>

namespace detent {

Result<OverrideTable, std::string> parse_cli_overrides(const std::vector<std::string>& raw_pairs) {
	OverrideTable table;

	for (const auto& raw : raw_pairs) {
		auto eq = raw.find('=');
		if (eq == std::string::npos) {
			return std::string("malformed --just-knob value (expected name=value): " + raw);
		}
		std::string name = raw.substr(0, eq);
		std::string value = raw.substr(eq + 1);

		if (!is_valid_knob_name(name)) {
			return std::string("invalid knob name in override: " + name);
		}
		if (table.values.find(name) != table.values.end()) {
			return std::string("duplicate --just-knob override for: " + name);
		}

		if (value == "true") {
			table.values[name] = OverrideValue{true};
		} else if (value == "false") {
			table.values[name] = OverrideValue{false};
		} else {
			int errno_save = 0;
			auto parsed = strict_stoll(&errno_save, value, nullptr, 10);
			if (errno_save != 0) {
				return std::string("invalid override value for " + name + ": " + value);
			}
			table.values[name] = OverrideValue{static_cast<std::int64_t>(parsed)};
		}
	}

	return table;
}

} // namespace detent
