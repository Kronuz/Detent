#pragma once

// Parsing/serializing internals shared between json_schema.cc
// (parse_defaults/parse_source_snapshot/serialize_snapshot) and
// json_patch.cc (parse_snapshot_patch/serialize_patch): the per-knob object
// grammar, the per-knob JSON writer, and the JSON-nesting-depth DoS guard
// (security review finding F1) must be identical in both places, not
// reimplemented a second time and risk drifting apart. Not part of the
// public read/eval API -- internal to the detent library's own parsing
// code, the same role validation.hh's free functions play for name/group-
// key checks.

#include "detent/errors.hh"
#include "detent/model.hh"
#include "detent/result.hh"

#include <rapidjson/document.h>

#include <string>
#include <string_view>

namespace detent::internal {

// See json_schema.cc's kMaxJsonNestingDepth comment for why this exists:
// rapidjson's stock recursive-descent Parse() has no depth limit and a
// few-hundred-thousand-deep nested-bracket payload overruns the thread
// stack before any of this file's own size/count validators ever run.
inline constexpr int kMaxJsonNestingDepth = 64;

[[nodiscard]] bool nesting_depth_exceeds(std::string_view json_text, int max_depth);

// Parses one knob's JSON object (the value half of a "knobs" map entry) into
// a KnobDef. Shared by a full snapshot's "knobs" object and a patch's
// "knobs" object -- a single knob's grammar (type/default/overrides/
// rollout_pct) is identical in both; only what surrounds it (a whole map vs.
// one map plus per-key `null`-for-delete) differs.
[[nodiscard]] Result<KnobDef, UpdateError> parse_knob_def(const rapidjson::Value& v, const std::string& name);

// Writes one KnobDef into a rapidjson object value, the exact inverse of
// parse_knob_def. Shared by serialize_snapshot (every knob) and
// serialize_patch (only the changed/added ones).
[[nodiscard]] rapidjson::Value write_knob_def(const KnobDef& def, rapidjson::Document::AllocatorType& alloc);

} // namespace detent::internal
