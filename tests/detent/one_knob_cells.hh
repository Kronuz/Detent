// A1's cells, shared by the recorder (one_knob_record.cc, built against main) and the
// differential test: every former bool shape the evaluation tests cover, read with no hash and
// 200 hashes, in no group, a listed group and an unlisted one. Plain data and JSON only, so the
// same enumeration compiles against both trees.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace one_knob_cells {

inline constexpr const char *kName = "ns:k";

// Group "g": none, forced on, forced off, or a percentage.
struct Table {
	enum Kind { None, ForcedOn, ForcedOff, Percentage } kind = None;
	std::uint8_t pct = 0;
};

struct Definition {
	bool fallback = false;
	std::optional<std::uint8_t> rollout;
	Table table;
	bool has_percentage() const { return rollout || table.kind == Table::Percentage; }
};

inline std::vector<Definition> definitions() {
	const std::optional<std::uint8_t> rollouts[] = {std::nullopt, 0, 1, 50, 99, 100};
	std::vector<Table> tables{{}, {Table::ForcedOn}, {Table::ForcedOff}};
	for (std::uint8_t pct : {0, 1, 50, 99, 100})
		tables.push_back({Table::Percentage, pct});
	std::vector<Definition> result;
	for (bool fallback : {false, true})
		for (const auto &rollout : rollouts)
			for (const auto &table : tables)
				result.push_back({fallback, rollout, table});
	return result;
}

inline std::vector<std::optional<std::string>> hashes() {
	std::vector<std::optional<std::string>> result{std::nullopt};
	for (unsigned i = 0; i != 200; ++i)
		result.push_back("user-" + std::to_string(i));
	return result;
}

inline std::vector<std::optional<std::string>> groups() { return {std::nullopt, "g", "other"}; }

// main's two-type JSON for the definition.
inline std::string main_json(const Definition &d) {
	std::string json = R"({"schema_version":1,"knobs":{"ns:k":{"type":"bool","default":)";
	json += d.fallback ? "true" : "false";
	if (d.rollout)
		json += ",\"rollout_pct\":" + std::to_string(*d.rollout);
	if (d.table.kind == Table::ForcedOn) json += R"(,"overrides":{"g":true})";
	if (d.table.kind == Table::ForcedOff) json += R"(,"overrides":{"g":false})";
	if (d.table.kind == Table::Percentage)
		json += R"(,"overrides":{"g":{"rollout_pct":)" + std::to_string(d.table.pct) + "}}";
	return json + "}}}";
}

// The same knob as one integer definition: default 0/1, rollout value 1 with any percentage.
inline std::string one_knob_json(const Definition &d) {
	std::string json = R"({"schema_version":1,"knobs":{"ns:k":{"default":)";
	json += d.fallback ? "1" : "0";
	if (d.rollout)
		json += ",\"rollout_pct\":" + std::to_string(*d.rollout);
	if (d.has_percentage())
		json += ",\"rollout_value\":1";
	if (d.table.kind == Table::ForcedOn) json += R"(,"overrides":{"g":1})";
	if (d.table.kind == Table::ForcedOff) json += R"(,"overrides":{"g":0})";
	if (d.table.kind == Table::Percentage)
		json += R"(,"overrides":{"g":{"rollout_pct":)" + std::to_string(d.table.pct) + "}}";
	return json + "}}}";
}

// The percentage that decides a cell: a matching group percentage, else the knob-wide rollout
// when no group override matches; a matching forced group decides without one.
inline std::optional<std::uint8_t> applied_percentage(const Definition &d, const std::optional<std::string> &group) {
	if (group && *group == "g" && d.table.kind != Table::None)
		return d.table.kind == Table::Percentage ? std::optional<std::uint8_t>(d.table.pct) : std::nullopt;
	return d.rollout;
}

} // namespace one_knob_cells
