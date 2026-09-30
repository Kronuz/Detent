#pragma once

// Section 4: strict JSON decoding of both the baked-defaults file and a
// source snapshot payload, into the in-memory Snapshot model. Every schema
// violation rejects the *whole* input -- there is no partial acceptance.

#include "detent/errors.hh"
#include "detent/model.hh"
#include "detent/result.hh"

#include <cstddef>
#include <string_view>

namespace detent {

// schema_version must be exactly 1 in both files. Baked defaults carry no
// "version" field (it is implicitly 0); a source snapshot requires a
// positive "version" field.
inline constexpr std::size_t kDefaultMaxKnobs = 100'000;
inline constexpr std::size_t kDefaultMaxBytes = 16u * 1024 * 1024; // 16 MiB

// Parses `just_knobs.json`-shaped bytes into a version-0 Snapshot.
[[nodiscard]] Result<Snapshot, UpdateError> parse_defaults(
	std::string_view json_text,
	std::string source_id = "defaults",
	std::size_t max_knobs = kDefaultMaxKnobs,
	std::size_t max_bytes = kDefaultMaxBytes);

// Parses a source snapshot payload. Requires `version` to be a positive
// integer; the caller is responsible for checking it is strictly greater
// than the last accepted version (Section 4: version rules).
[[nodiscard]] Result<Snapshot, UpdateError> parse_source_snapshot(
	std::string_view json_text,
	std::string source_id,
	std::size_t max_knobs = kDefaultMaxKnobs,
	std::size_t max_bytes = kDefaultMaxBytes);

// Section 4 merge contract: every accepted source snapshot is a complete
// live overlay over the baked defaults. A key present in `live` replaces the
// *entire* definition from `defaults`; a key absent from `live` retains its
// baked definition untouched. No field-wise merge.
[[nodiscard]] Snapshot merge_over_defaults(const Snapshot& defaults, const Snapshot& live);

// Serializes a Snapshot back to a source-snapshot-shaped JSON payload
// (schema_version/version/published_at/knobs), the exact inverse of
// parse_source_snapshot. Not part of the read/update hot path -- this exists
// for reference publisher tooling (see examples/publish_snapshot.cc) and for
// round-trip tests, since the client itself never needs to re-emit JSON.
[[nodiscard]] std::string serialize_snapshot(const Snapshot& snapshot);

} // namespace detent
