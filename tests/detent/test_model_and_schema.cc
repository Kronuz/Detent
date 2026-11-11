// Schema tests: both example files parse to the expected map,
// every violation in the schema table rejects the whole input, and the merge
// contract does whole-definition replacement, never field-wise.
#include <cassert>
#include <cstdio>
#include <string>

#include "detent/json_schema.hh"
#include "detent/validation.hh"

using namespace detent;

namespace {

void test_defaults_parse() {
	constexpr std::string_view defaults = R"({
		"schema_version": 1,
		"knobs": {
			"checkout:killswitch_new_path": {
				"default": 0, "overrides": {}, "rollout_pct": null, "rollout_value": null
			},
			"search:max_results": { "default": 25 }
		}
	})";

	auto r = parse_defaults(defaults);
	assert(r.has_value());
	const auto& snap = r.value();
	assert(snap.version == 0);
	assert(snap.knobs.size() == 2);

	const auto& killswitch = snap.knobs.find("checkout:killswitch_new_path")->second;
	assert(killswitch.default_value == 0);
	assert(killswitch.overrides.empty());
	assert(!killswitch.rollout_pct.has_value() && !killswitch.rollout_value.has_value());
	assert(snap.knobs.find("search:max_results")->second.default_value == 25);

	std::puts("defaults parse OK");
}

void test_source_parse_and_merge() {
	constexpr std::string_view defaults = R"({
		"schema_version": 1,
		"knobs": {
			"checkout:killswitch_new_path": { "default": 0 },
			"search:max_results": { "default": 25 }
		}
	})";
	constexpr std::string_view source = R"({
		"schema_version": 1,
		"version": 42,
		"published_at": "2026-09-29T15:00:00Z",
		"knobs": {
			"checkout:killswitch_new_path": {
				"default": 0, "overrides": { "employees": 1 }, "rollout_pct": 10, "rollout_value": 1
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
	assert(merged.knobs.find("search:max_results")->second.default_value == 25);

	// Source overrode "checkout:...": the WHOLE definition replaced, not
	// merged field-by-field.
	const auto& killswitch = merged.knobs.find("checkout:killswitch_new_path")->second;
	assert(killswitch.rollout_pct.has_value() && *killswitch.rollout_pct == 10);
	assert(killswitch.rollout_value == std::optional<std::int64_t>(1));
	assert(std::get<std::int64_t>(killswitch.overrides.find("employees")->second) == 1);

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
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"default":0,"bogus":1}}})").has_value());
	// Invalid knob name (no colon).
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"nocolon":{"default":0}}})").has_value());
	// Malformed JSON entirely.
	assert(!parse_defaults(R"({"schema_version":1,)").has_value());
	// A knob missing its required "default" rejects the whole input.
	assert(!parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{}}})").has_value());
	{
		// rapidjson's DOM keeps every member on a duplicate object key, so a
		// literal duplicate "a:b" requires building the object text by hand
		// (a C++ string literal can't have two identical keys and still be
		// "the same" key twice at the language level, but the JSON *text*
		// can) -- this tests the top-level knob-name duplicate-rejection
		// fix (previously silently last-definition-wins via map
		// operator[]).
		std::string dup_knob_name =
		    R"({"schema_version":1,"knobs":{"a:b":{"default":1},"a:b":{"default":2}}})";
		assert(!parse_defaults(dup_knob_name).has_value());
	}
	// Duplicate override key within one knob's "overrides" object.
	{
		std::string dup_override_key =
		    R"({"schema_version":1,"knobs":{"a:b":{"default":0,)"
		    R"("overrides":{"g":1,"g":0}}}})";
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
	// a server's ingest sink -- an arbitrary
	// InitOptions::source_poll implementation would otherwise have no cap.
	{
		std::string oversized = R"({"schema_version":1,"version":1,"knobs":{})";
		oversized.append(std::string(64, ' ')); // pad past a tiny max_bytes
		oversized += "}";
		assert(!parse_source_snapshot(oversized, "s", kDefaultMaxKnobs, /*max_bytes=*/16).has_value());
	}

	std::puts("schema violations reject whole input OK");
}

// A2: the definition's shape. Each invalid shape is a Schema rejection of the whole input.
void expect_schema_rejection(std::string_view knob) {
	const auto text = std::string(R"({"schema_version":1,"knobs":{"a:b":)") + std::string(knob) + "}}";
	auto r = parse_defaults(text);
	assert(!r.has_value());
	assert(r.error().reason == UpdateRejectReason::Schema);
}

void test_definition_shape() {
	// rollout_value without any percentage.
	expect_schema_rejection(R"({"default":25,"rollout_value":50})");
	expect_schema_rejection(R"({"default":25,"overrides":{"beta":7},"rollout_value":50})");
	// A percentage (knob-wide or group) without rollout_value.
	expect_schema_rejection(R"({"default":25,"rollout_pct":10})");
	expect_schema_rejection(R"({"default":25,"overrides":{"canary":{"rollout_pct":50}}})");
	// Percentages over 100.
	expect_schema_rejection(R"({"default":25,"rollout_pct":101,"rollout_value":50})");
	expect_schema_rejection(R"({"default":25,"overrides":{"canary":{"rollout_pct":101}},"rollout_value":50})");
	// No type tag, no boolean literals.
	expect_schema_rejection(R"({"type":"int","default":25})");
	expect_schema_rejection(R"({"default":true})");
	expect_schema_rejection(R"({"default":0,"overrides":{"g":true}})");
	expect_schema_rejection(R"({"default":0,"rollout_pct":10,"rollout_value":true})");
	// A string or fractional value is not an integer.
	expect_schema_rejection(R"({"default":"25"})");
	expect_schema_rejection(R"({"default":2.5})");

	// definition_error agrees with the parser on the in-memory model.
	KnobDef def;
	assert(definition_error(def) == nullptr);
	def.rollout_value = 1;
	assert(definition_error(def) != nullptr);
	def.rollout_pct = 10;
	assert(definition_error(def) == nullptr);
	def.rollout_pct = 101;
	assert(definition_error(def) != nullptr);
	def.rollout_pct.reset();
	def.overrides["g"] = GroupRollout{50};
	assert(definition_error(def) == nullptr);
	def.rollout_value.reset();
	assert(definition_error(def) != nullptr);

	std::puts("definition shape rejections OK");
}

// A2: an integer knob with a rollout and group overrides parses and
// serialize_snapshot writes back exactly the canonical text.
void test_integer_knob_round_trip() {
	constexpr std::string_view canonical =
		R"({"schema_version":1,"version":3,"knobs":{"search:batch_size":{"default":25,)"
		R"("overrides":{"beta":7,"canary":{"rollout_pct":50}},"rollout_pct":10,"rollout_value":50}}})";
	auto parsed = parse_source_snapshot(canonical, "s");
	assert(parsed.has_value());
	const auto& def = parsed.value().knobs.find("search:batch_size")->second;
	assert(def.default_value == 25 && def.rollout_pct == std::optional<std::uint8_t>(10) &&
	       def.rollout_value == std::optional<std::int64_t>(50));
	assert(std::get<std::int64_t>(def.overrides.find("beta")->second) == 7);
	assert(std::get<GroupRollout>(def.overrides.find("canary")->second).rollout_pct == 50);
	assert(serialize_snapshot(parsed.value()) == canonical);

	// Field order in the input does not matter; the output is canonical.
	constexpr std::string_view shuffled =
		R"({"schema_version":1,"version":3,"knobs":{"search:batch_size":{"rollout_value":50,)"
		R"("rollout_pct":10,"overrides":{"canary":{"rollout_pct":50},"beta":7},"default":25}}})";
	auto reordered = parse_source_snapshot(shuffled, "s");
	assert(reordered.has_value() && serialize_snapshot(reordered.value()) == canonical);
	assert(reordered.value().knobs.find("search:batch_size")->second == def);

	// A plain value writes only its default.
	constexpr std::string_view plain = R"({"schema_version":1,"version":1,"knobs":{"a:b":{"default":-9}}})";
	auto minimal = parse_source_snapshot(plain, "s");
	assert(minimal.has_value() && serialize_snapshot(minimal.value()) == plain);

	std::puts("integer knob round trip OK");
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
	test_definition_shape();
	test_integer_knob_round_trip();
	test_name_validation();
	return 0;
}
