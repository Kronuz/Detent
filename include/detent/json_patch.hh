#pragma once

// Partial-update primitives: parse/apply/diff/serialize a SnapshotPatch (see
// model.hh) -- the wire format that lets a caller send or receive only the
// knobs that changed instead of a full snapshot. Two independent uses of
// the exact same shape:
//   - Write side: detentd's PATCH /v1/snapshot accepts a SnapshotPatch and
//     apply_patch()es it onto the current snapshot server-side, instead of
//     requiring a publisher to resend every unrelated knob just to flip
//     one kill switch.
//   - Read side: detentd caches diff_snapshots(previous, current) once per
//     accepted update (the same "compute once, not once per request"
//     principle current_etag_ already applies) and serves it to a poller
//     whose last-known version is exactly that previous one, instead of
//     the full body -- see route_get_snapshot's X-Detent-Base-Version
//     handling.

#include "detent/errors.hh"
#include "detent/json_schema.hh" // kDefaultMaxKnobs/kDefaultMaxBytes
#include "detent/model.hh"
#include "detent/result.hh"

#include <cstddef>
#include <string>
#include <string_view>

namespace detent {

// Parses a PATCH-shaped payload:
//   {"schema_version":1,"version":N,"knobs":{"name": <KnobDef>|null, ...}}
// A `null` knob value means "delete this knob from the live overlay"; any
// other value is validated with the exact same per-knob grammar a full
// snapshot's knobs use (parse_source_snapshot). `version` is required and
// must be positive, same as a full snapshot; the caller is responsible for
// checking it is strictly greater than the version this patch would apply
// on top of.
[[nodiscard]] Result<SnapshotPatch, UpdateError> parse_snapshot_patch(
	std::string_view json_text,
	std::string source_id,
	std::size_t max_knobs = kDefaultMaxKnobs,
	std::size_t max_bytes = kDefaultMaxBytes);

// Applies `patch` onto `base` (a full, already-validated Snapshot),
// producing a new full Snapshot: every patched knob is upserted or (if its
// patch value is nullopt) removed; every other knob is carried over from
// `base` untouched. Rejects if the RESULT would exceed `max_knobs` -- a
// patch can grow the knob count by adding new names, so this check can't
// simply be inherited from the (already knob-count-capped) inputs.
[[nodiscard]] Result<Snapshot, UpdateError> apply_patch(
	const Snapshot& base, const SnapshotPatch& patch, std::size_t max_knobs = kDefaultMaxKnobs);

// Computes the minimal patch that turns `from` into `to`: every knob whose
// definition differs (or is newly present in `to`) is included in full;
// every knob present in `from` but absent from `to` is included as a
// deletion (nullopt). A knob identical in both (KnobDef's == compares the
// full BoolDef/IntDef value, including overrides/rollout_pct) is omitted
// entirely -- this is what keeps a single-knob change's diff small
// regardless of total knob count, the whole point of this file.
[[nodiscard]] SnapshotPatch diff_snapshots(const Snapshot& from, const Snapshot& to);

// Composes two sequential patches (`older` applied before `newer`) into
// one patch with the same net effect: for each knob, `newer` wins if
// present, otherwise `older`'s entry carries over. Result targets
// `newer.version`. Valid only when `newer` was computed starting from the
// state `older` produces (i.e. the two are adjacent steps in one history).
//
// Applying the result always reconstructs the exact target state, but the
// result is not always byte-minimal: a knob touched in both `older` and
// `newer` that ends up back at its original value (e.g. off -> on -> off)
// still appears, since composing patches alone -- without the full
// snapshot at either end -- can't detect that its net effect is zero. Size
// is still bounded by the knobs touched across the composed steps, never
// by the full snapshot's knob count.
[[nodiscard]] SnapshotPatch compose_patches(const SnapshotPatch& older, const SnapshotPatch& newer);

// Serializes a SnapshotPatch to the same PATCH-shaped JSON
// parse_snapshot_patch expects -- the exact inverse, and the wire format
// both a PATCH publish and a poll's diff response use.
[[nodiscard]] std::string serialize_patch(const SnapshotPatch& patch);

} // namespace detent
