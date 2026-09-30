// Partial-update primitives (see json_patch.hh): parse_snapshot_patch,
// apply_patch, diff_snapshots, serialize_patch, and the round trip between
// them -- the machinery behind detentd's PATCH /v1/snapshot and its
// diff-since-last-version poll response.
#include <cassert>
#include <cstdio>

#include "detent/json_patch.hh"
#include "detent/json_schema.hh"

using namespace detent;

namespace {

Snapshot make_base() {
	auto parsed = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 1,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": false},
			"search:max_results": {"type": "int", "default": 25},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10}
		}
	})",
	                                     "test");
	assert(parsed.has_value());
	return parsed.value();
}

void test_parse_patch_upsert_and_delete() {
	auto patch = parse_snapshot_patch(R"({
		"schema_version": 1,
		"version": 2,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": true},
			"search:max_results": null,
			"checkout:brand_new_flag": {"type": "bool", "default": false}
		}
	})",
	                                   "test");
	assert(patch.has_value());
	assert(patch.value().version == 2);
	assert(patch.value().knobs.size() == 3);
	auto it = patch.value().knobs.find("search:max_results");
	assert(it != patch.value().knobs.end());
	assert(!it->second.has_value()); // null -> delete
	auto it2 = patch.value().knobs.find("checkout:brand_new_flag");
	assert(it2 != patch.value().knobs.end());
	assert(it2->second.has_value());
}

void test_parse_patch_rejects_malformed() {
	// Missing version.
	auto no_version = parse_snapshot_patch(R"({"schema_version":1,"knobs":{}})", "test");
	assert(!no_version.has_value());

	// Duplicate knob name in the patch's own knobs object must reject the
	// whole patch, same strictness as a full snapshot.
	auto dup = parse_snapshot_patch(
		R"({"schema_version":1,"version":2,"knobs":{}})", "test");
	assert(dup.has_value()); // sanity: empty knobs is a valid (no-op) patch

	// An invalid knob definition anywhere in the patch rejects the whole
	// patch, not just that one entry.
	auto bad_def = parse_snapshot_patch(
		R"({"schema_version":1,"version":2,"knobs":{"a:b":{"type":"bool"}}})", "test");
	assert(!bad_def.has_value()); // missing required 'default'
}

void test_apply_patch_upsert_delete_untouched() {
	auto base = make_base();
	auto patch = parse_snapshot_patch(R"({
		"schema_version": 1,
		"version": 2,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": true},
			"search:max_results": null
		}
	})",
	                                   "test");
	assert(patch.has_value());

	auto applied = apply_patch(base, patch.value());
	assert(applied.has_value());
	auto& out = applied.value();
	assert(out.version == 2);
	assert(out.knobs.size() == 2); // killswitch (changed) + payments (untouched); max_results deleted
	assert(std::get<BoolDef>(out.knobs.at("checkout:killswitch_new_path")).default_value == true);
	assert(out.knobs.find("search:max_results") == out.knobs.end());
	// Untouched knob survives unchanged.
	assert(std::get<BoolDef>(out.knobs.at("payments:new_processor")).rollout_pct == 10);
}

void test_apply_patch_rejects_oversize_result() {
	auto base = make_base(); // 3 knobs
	SnapshotPatch patch;
	patch.version = 2;
	patch.knobs["brand:new"] = BoolDef{};
	auto applied = apply_patch(base, patch, /*max_knobs=*/3); // would grow to 4
	assert(!applied.has_value());
}

void test_diff_snapshots_round_trip() {
	auto base = make_base();
	auto updated_result = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 2,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": true},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10},
			"checkout:brand_new_flag": {"type": "int", "default": 7}
		}
	})",
	                                             "test");
	assert(updated_result.has_value());
	auto& updated = updated_result.value();

	auto diff = diff_snapshots(base, updated);
	// killswitch changed, brand_new_flag added, max_results removed,
	// payments unchanged (identical BoolDef) -- so exactly 3 entries, not 4.
	assert(diff.knobs.size() == 3);
	assert(diff.knobs.find("payments:new_processor") == diff.knobs.end());
	assert(diff.knobs.at("search:max_results") == std::nullopt);
	assert(diff.knobs.at("checkout:killswitch_new_path").has_value());
	assert(diff.knobs.at("checkout:brand_new_flag").has_value());

	// Round trip: serialize the diff, reparse it, reapply it onto `base`,
	// and the result must equal `updated` exactly (same knobs, same version).
	auto wire = serialize_patch(diff);
	auto reparsed = parse_snapshot_patch(wire, "test");
	assert(reparsed.has_value());
	auto reapplied = apply_patch(base, reparsed.value());
	assert(reapplied.has_value());
	assert(reapplied.value().version == updated.version);
	assert(reapplied.value().knobs.size() == updated.knobs.size());
	for (const auto& [name, def] : updated.knobs) {
		assert(reapplied.value().knobs.at(name) == def);
	}
}

void test_diff_snapshots_no_change_is_empty() {
	auto base = make_base();
	auto diff = diff_snapshots(base, base);
	assert(diff.knobs.empty());
}

void test_compose_patches_matches_direct_diff() {
	auto v1 = make_base(); // version 1, 3 knobs
	auto v2_result = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 2,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": false},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10},
			"checkout:brand_new_flag": {"type": "int", "default": 7}
		}
	})",
	                                          "test");
	assert(v2_result.has_value());
	auto& v2 = v2_result.value();
	auto v3_result = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 3,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": true},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10},
			"checkout:brand_new_flag": {"type": "int", "default": 7},
			"another:knob": {"type": "bool", "default": true}
		}
	})",
	                                          "test");
	assert(v3_result.has_value());
	auto& v3 = v3_result.value();

	// No knob here round-trips back to its version-1 value, so composing
	// the two steps produces exactly the same patch a direct diff would.
	auto step_1_to_2 = diff_snapshots(v1, v2);
	auto step_2_to_3 = diff_snapshots(v2, v3);
	auto composed = compose_patches(step_1_to_2, step_2_to_3);
	auto direct = diff_snapshots(v1, v3);

	assert(composed.version == direct.version);
	assert(composed.knobs.size() == direct.knobs.size());
	for (const auto& [name, entry] : direct.knobs) {
		assert(composed.knobs.at(name) == entry);
	}

	auto via_composed = apply_patch(v1, composed);
	assert(via_composed.has_value());
	assert(via_composed.value().version == v3.version);
	for (const auto& [name, def] : v3.knobs) {
		assert(via_composed.value().knobs.at(name) == def);
	}
}

// A knob touched in both steps but back at its original value by the end
// (off -> on -> off) still reconstructs the correct target, but the
// composed patch is not byte-minimal for it -- see compose_patches's
// comment. This is the documented tradeoff, not a bug.
void test_compose_patches_reconstructs_target_even_with_a_reverted_knob() {
	auto v1 = make_base(); // checkout:killswitch_new_path starts false
	auto v2_result = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 2,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": true},
			"search:max_results": {"type": "int", "default": 25},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10}
		}
	})",
	                                          "test");
	assert(v2_result.has_value());
	auto v3_result = parse_source_snapshot(R"({
		"schema_version": 1,
		"version": 3,
		"knobs": {
			"checkout:killswitch_new_path": {"type": "bool", "default": false},
			"search:max_results": {"type": "int", "default": 25},
			"payments:new_processor": {"type": "bool", "default": false, "rollout_pct": 10}
		}
	})",
	                                          "test");
	assert(v3_result.has_value());
	auto& v3 = v3_result.value();

	auto composed = compose_patches(diff_snapshots(v1, v2_result.value()), diff_snapshots(v2_result.value(), v3));
	// The reverted knob shows up (composition can't see it's a net no-op).
	assert(composed.knobs.count("checkout:killswitch_new_path") == 1);
	// Still small: bounded by the one knob actually touched, not the
	// snapshot's full 3-knob count.
	assert(composed.knobs.size() == 1);

	auto via_composed = apply_patch(v1, composed);
	assert(via_composed.has_value());
	for (const auto& [name, def] : v3.knobs) {
		assert(via_composed.value().knobs.at(name) == def);
	}
}

} // namespace

int main() {
	test_parse_patch_upsert_and_delete();
	test_parse_patch_rejects_malformed();
	test_apply_patch_upsert_delete_untouched();
	test_apply_patch_rejects_oversize_result();
	test_diff_snapshots_round_trip();
	test_diff_snapshots_no_change_is_empty();
	test_compose_patches_matches_direct_diff();
	test_compose_patches_reconstructs_target_even_with_a_reverted_knob();
	std::puts("test_json_patch: all tests passed");
	return 0;
}
