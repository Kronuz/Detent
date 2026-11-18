#pragma once

// In-memory data model (docs/design/knob-model.md, docs/design/one-knob-type.md). A knob is one
// int64 definition; Snapshot is the immutable, fully validated map readers see at one instant.

#include "detent/flat_map.hh"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace detent {

using KnobName = std::string;

// A group's percentage: serves the definition's rollout value to that share of hashes.
struct GroupRollout {
	std::uint8_t rollout_pct = 0;
	[[nodiscard]] bool operator==(const GroupRollout&) const = default;
};
using GroupOverride = std::variant<std::int64_t, GroupRollout>;   // forced value or percentage

// One knob: a default, per-group overrides, and an optional knob-wide rollout. A bool is
// default 0 with rollout value 1. `rollout_value` is present exactly when some percentage
// (knob-wide or group) is, and `definition_error` checks that shape.
struct KnobDef {
	std::int64_t default_value = 0;
	flat_map<std::string, GroupOverride, std::less<>> overrides;   // group key -> forced value or percentage
	std::optional<std::uint8_t> rollout_pct;   // inclusive 0..100
	std::optional<std::int64_t> rollout_value;

	[[nodiscard]] bool operator==(const KnobDef&) const = default;
};

// The first shape error of `definition`, or nullptr: a percentage over 100, a rollout value
// without a percentage, or a percentage without a rollout value. Keys are checked by callers.
[[nodiscard]] inline const char* definition_error(const KnobDef& definition) noexcept {
	bool ramped = definition.rollout_pct.has_value();
	if (ramped && *definition.rollout_pct > 100)
		return "rollout_pct out of range";
	for (const auto& [key, value] : definition.overrides)
		if (const auto* rollout = std::get_if<GroupRollout>(&value)) {
			if (rollout->rollout_pct > 100)
				return "group rollout_pct out of range";
			ramped = true;
		}
	if (ramped != definition.rollout_value.has_value())
		return ramped ? "rollout_pct without rollout_value" : "rollout_value without rollout_pct";
	return nullptr;
}

// Immutable, fully validated map plus the metadata a diagnostics endpoint
// wants. Snapshot instances are never mutated after construction; the
// updater builds a brand new one and publishes it via SnapshotRef.
struct Snapshot {
	std::uint64_t version = 0;              // 0 == baked-default startup snapshot
	std::optional<std::string> published_at; // RFC 3339 UTC, source-provided
	std::string source_id;                   // "defaults", "file:<path>", ...
	flat_map<KnobName, KnobDef, std::less<>> knobs;
};

// A partial update: only the knobs that changed, not a full snapshot.
// Absent knobs are untouched; a knob mapped to std::nullopt means "remove
// this knob from the live overlay" (it falls back to its baked default, if
// any -- the same state as never having been overlaid), matching JSON Merge
// Patch's null-means-delete convention (RFC 7396) at the per-knob
// granularity the model already treats knobs as atomic units at (a live key
// replaces the entire definition; no field-wise merge). Used both as the
// wire format for a PATCH publish (a caller sending only the few knobs it
// changed) and as the wire format for a poll's diff-since-last-version
// response (see json_patch.hh) -- the same shape either direction.
struct SnapshotPatch {
	std::uint64_t version = 0;
	flat_map<KnobName, std::optional<KnobDef>, std::less<>> knobs;
};

} // namespace detent
