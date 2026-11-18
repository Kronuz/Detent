#pragma once

// Per-process CLI overrides. Immutable after startup; shadows
// both baked defaults and live snapshots; remains active across swaps. The
// unset path is one atomic load of a null pointer and a branch -- no map
// access at all (see SnapshotRef / Client::get).

#include "detent/result.hh"

#include "detent/flat_map.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace detent {

using OverrideValue = std::int64_t;

struct OverrideTable {
	flat_map<std::string, OverrideValue, std::less<>> values;
};

// Parses repeatable "name=value" pairs (the part after "--knob "; CLI
// flag splitting itself is the caller's job, not this library's). Exact
// lowercase "true"/"false" parse as 1/0; anything else parses as signed
// base-10 i64. Duplicate names are a startup error -- no
// last-one-wins. Returns a human-readable error naming the offending
// name/value on failure.
[[nodiscard]] Result<OverrideTable, std::string> parse_cli_overrides(const std::vector<std::string>& raw_pairs);

} // namespace detent
