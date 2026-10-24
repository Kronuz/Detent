#pragma once

// Section 4: in-memory data model. KnobDef is a closed tagged union (bool or
// int); Snapshot is the immutable, fully validated map readers see at one
// instant. Both are FILLED BLANK per the spec -- the shape is a clone choice,
// not a claim about Meta's private representation.

#include "detent/flat_map.hh"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace detent {

using KnobName = std::string;

struct GroupRollout {
	std::uint8_t rollout_pct = 0;
	[[nodiscard]] bool operator==(const GroupRollout&) const = default;
};
using GroupOverride = std::variant<bool, GroupRollout>;

struct BoolDef {
	bool default_value = false;
	flat_map<std::string, GroupOverride, std::less<>> overrides;   // group key -> forced value or percentage
	std::optional<std::uint8_t> rollout_pct;   // inclusive 0..100

	[[nodiscard]] bool operator==(const BoolDef&) const = default;
};

struct IntDef {
	std::int64_t default_value = 0;

	[[nodiscard]] bool operator==(const IntDef&) const = default;
};

using KnobDef = std::variant<BoolDef, IntDef>;

[[nodiscard]] inline bool is_bool(const KnobDef& def) noexcept {
	return std::holds_alternative<BoolDef>(def);
}
[[nodiscard]] inline bool is_int(const KnobDef& def) noexcept {
	return std::holds_alternative<IntDef>(def);
}

// Immutable, fully validated map plus the metadata a diagnostics endpoint
// wants. Snapshot instances are never mutated after construction; the
// updater builds a brand new one and publishes it via SnapshotRef.
struct Snapshot {
	std::uint64_t version = 0;              // 0 == baked-default startup snapshot
	std::optional<std::string> published_at; // RFC 3339 UTC, source-provided
	std::string source_id;                   // "defaults", "file:<path>", ...
	flat_map<KnobName, KnobDef, std::less<>> knobs;

	[[nodiscard]] std::size_t bool_count() const noexcept {
		std::size_t n = 0;
		for (const auto& [name, def] : knobs) n += is_bool(def) ? 1 : 0;
		return n;
	}
	[[nodiscard]] std::size_t int_count() const noexcept {
		std::size_t n = 0;
		for (const auto& [name, def] : knobs) n += is_int(def) ? 1 : 0;
		return n;
	}
};

// A partial update: only the knobs that changed, not a full snapshot.
// Absent knobs are untouched; a knob mapped to std::nullopt means "remove
// this knob from the live overlay" (it falls back to its baked default, if
// any -- the same state as never having been overlaid), matching JSON Merge
// Patch's null-means-delete convention (RFC 7396) at the per-knob
// granularity the model already treats knobs as atomic units at (Section 4:
// "a key present in live replaces the ENTIRE definition... no field-wise
// merge"). Used both as the wire format for a PATCH publish (a caller
// sending only the few knobs it changed) and as the wire format for a
// poll's diff-since-last-version response (see json_patch.hh) -- the same
// shape either direction.
struct SnapshotPatch {
	std::uint64_t version = 0;
	flat_map<KnobName, std::optional<KnobDef>, std::less<>> knobs;
};

} // namespace detent
