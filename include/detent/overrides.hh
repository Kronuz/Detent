#pragma once

// Section 11: per-process CLI overrides. Immutable after startup; shadows
// both baked defaults and live snapshots; remains active across swaps. The
// unset path is one atomic load of a null pointer and a branch -- no map
// access at all (see SnapshotRef / Client::eval).

#include "detent/result.hh"

#include "detent/flat_map.hh"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace detent {

using OverrideValue = std::variant<bool, std::int64_t>;

struct OverrideTable {
	flat_map<std::string, OverrideValue, std::less<>> values;
};

// Parses repeatable "name=value" pairs (the part after "--just-knob "; CLI
// flag splitting itself is the caller's job, not this library's). Bool
// parses only from exact lowercase "true"/"false"; anything else parses as
// signed base-10 i64. Duplicate names are a startup error -- no
// last-one-wins. Returns a human-readable error naming the offending
// name/value on failure.
[[nodiscard]] Result<OverrideTable, std::string> parse_cli_overrides(const std::vector<std::string>& raw_pairs);

} // namespace detent
