// Section 4 schema tests: both example files parse to the expected map,
// every violation in the schema table rejects the whole input, and the merge
// contract does whole-definition replacement, never field-wise.
#include <cassert>
#include <cstdio>

#include "detent/json_schema.hh"
#include "detent/validation.hh"

using namespace detent;

namespace {

void test_defaults_parse() {
	constexpr std::string_view defaults = R"({
		"schema_version": 1,
		"knobs": {
			"checkout:killswitch_new_path": {
				"type": "bool", "default": false,
				"overrides": {}, "rollout_pct": null
			},
			"search:max_results": { "type": "int", "default": 25 }
		}
	})";

	auto r = parse_defaults(defaults);
	assert(r.has_value());
	const auto& snap = r.value();
	assert(snap.version == 0);
	assert(snap.knobs.size() == 2);
	assert(snap.bool_count() == 1);
	assert(snap.int_count() == 1);

	const auto* bd = std::get_if<BoolDef>(&snap.knobs.find("checkout:killswitch_new_path")->second);
	assert(bd != nullptr);
	assert(bd->default_value == false);
	assert(!bd->rollout_pct.has_value());

	std::puts("defaults parse OK");
}

void test_source_parse_and_merge() {
	constexpr std::string_view defaults = R"({
		"schema_version": 1,
		"knobs": {
			"checkout:killswitch_new_path": { "type": "bool", "default": false },
			"search:max_results": { "type": "int", "default": 25 }
		}
	})";
	constexpr std::string_view source = R"({
		"schema_version": 1,
		"version": 42,
		"published_at": "2026-09-29T15:00:00Z",
		"knobs": {
			"checkout:killswitch_new_path": {
				"type": "bool", "default": false,
				"overrides": { "employees": true }, "rollout_pct": 10
			}
		}
	})";

	auto d = parse_defaults(defaults);
	auto s = parse_source_snapshot(source, "test-source");
	assert(d.has_value());
	assert(s.has_value());
	assert(s.value().version == 42);
	assert(s.value().published_at == "2026-09-29T15:00:00Z");

	auto merged = merge_over_defaults(d.value(), s.value());
	assert(merged.version == 42);
	// Source omits "search:max_results" -> baked default retained verbatim.
	assert(merged.knobs.size() == 2);
	const auto* max_results = std::get_if<IntDef>(&merged.knobs.find("search:max_results")->second);
	assert(max_results != nullptr && max_results->default_value == 25);

	// Source overrode "checkout:...": the WHOLE definition replaced, not
	// merged field-by-field.
	const auto* killswitch = std::get_if<BoolDef>(&merged.knobs.find("checkout:killswitch_new_path")->second);
	assert(killswitch != nullptr);
	assert(killswitch->rollout_pct.has_value() && *killswitch->rollout_pct == 10);
	assert(std::get<bool>(killswitch->overrides.find("employees")->second) == true);

	std::puts("source parse + merge OK");
}

void test_schema_violations_reject_whole_input() {
	// Unknown top-level field.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{},"bogus":1})").has_value());
	// Wrong schema_version.
	assert(!parse_defaults(R"({"schema_version":2,"knobs":{}})").has_value());
	// Missing schema_version.
	assert(!parse_defaults(R"({"knobs":{}})").has_value());
	// version present in a defaults file is rejected (only valid in source).
	assert(!parse_defaults(R"({"schema_version":1,"version":1,"knobs":{}})").has_value());
	// version <= 0 in a source snapshot.
	assert(!parse_source_snapshot(R"({"schema_version":1,"version":0,"knobs":{}})", "s").has_value());
	// Missing version in a source snapshot.
	assert(!parse_source_snapshot(R"({"schema_version":1,"knobs":{}})", "s").has_value());
	// Unknown field on a knob definition.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":false,"bogus":1}}})")
	            .has_value());
	// Wrong type tag.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"string","default":false}}})")
	            .has_value());
	// rollout_pct out of range.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":false,"rollout_pct":101}}})")
	            .has_value());
	// Invalid knob name (no colon).
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"nocolon":{"type":"bool","default":false}}})")
	            .has_value());
	// Malformed JSON entirely.
	assert(!parse_defaults(R"({"schema_version":1,)").has_value());
	// Bool knob missing required "default" -- previously silently defaulted
	// to false instead of rejecting the whole input (found by two
	// independent review agents in the same session).
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"bool"}}})").has_value());
	// Int knob missing required "default" -- same bug, other type.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"int"}}})").has_value());
	{
		// rapidjson's DOM keeps every member on a duplicate object key, so a
		// literal duplicate "a:b" requires building the object text by hand
		// (a C++ string literal can't have two identical keys and still be
		// "the same" key twice at the language level, but the JSON *text*
		// can) -- this tests the top-level knob-name duplicate-rejection
		// fix (previously silently last-definition-wins via map
		// operator[]).
		std::string dup_knob_name =
		    R"({"schema_version":1,"knobs":{"a:b":{"type":"int","default":1},"a:b":{"type":"int","default":2}}})";
		assert(!parse_defaults(dup_knob_name).has_value());
	}
	// Duplicate override key within one knob's "overrides" object.
	{
		std::string dup_override_key =
		    R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":false,)"
		    R"("overrides":{"g":true,"g":false}}}})";
		assert(!parse_defaults(dup_override_key).has_value());
	}
	// Duplicate top-level field (e.g. two "schema_version" entries).
	{
		std::string dup_top_level = R"({"schema_version":1,"schema_version":1,"knobs":{}})";
		assert(!parse_defaults(dup_top_level).has_value());
	}
	// Unbounded JSON nesting depth: a real, reproduced, unauthenticated
	// stack-exhaustion crash before this fix (security review finding F1).
	{
		std::string deep(2000, '[');
		deep.append(2000, ']');
		assert(!parse_defaults(deep).has_value());
	}
	// Byte-size cap enforced by parse_source_snapshot itself, not only by
	// detentd's own PUT-ingest CappedBodySink -- an arbitrary
	// InitOptions::source_poll implementation or a raw HTTP response body
	// had no cap at this layer before this fix.
	{
		std::string oversized = R"({"schema_version":1,"version":1,"knobs":{})";
		oversized.append(std::string(64, ' ')); // pad past a tiny max_bytes
		oversized += "}";
		assert(!parse_source_snapshot(oversized, "s", kDefaultMaxKnobs, /*max_bytes=*/16).has_value());
	}

	std::puts("schema violations reject whole input OK");
}

void test_name_validation() {
	assert(is_valid_knob_name("checkout:killswitch_new_path"));
	assert(is_valid_knob_name("a.b-c/d_e:f.g-h/i_j"));
	assert(!is_valid_knob_name(""));
	assert(!is_valid_knob_name("nocolon"));
	assert(!is_valid_knob_name("too:many:colons"));
	assert(!is_valid_knob_name(":emptyknobset"));
	assert(!is_valid_knob_name("emptylocal:"));
	assert(!is_valid_knob_name(std::string(201, 'a') + ":b"));

	std::puts("name validation OK");
}

} // namespace

int main() {
	test_defaults_parse();
	test_source_parse_and_merge();
	test_schema_violations_reject_whole_input();
	test_name_validation();
	return 0;
}
