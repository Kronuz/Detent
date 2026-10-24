// Duplicate-rejection precedence for the bulk-built model tables. Entries are
// appended in input order and sorted once, but the first error in INPUT order
// must still win, with the exact messages the per-entry insertion produced:
//   [a, b, a, invalid c] -> duplicate a        (the duplicate comes first)
//   [a, invalid c, a]    -> invalid c          (the invalid entry comes first)
// for knob names (snapshot and patch) and for override keys within a knob.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "detent/json_patch.hh"
#include "detent/json_schema.hh"

using namespace detent;

#define CHECK(cond)                                                                      \
	do {                                                                                 \
		if (!(cond)) {                                                                   \
			std::fprintf(stderr, "%s:%d CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::abort();                                                                \
		}                                                                                \
	} while (0)

namespace {

constexpr const char* kA = R"("a:a":{"type":"int","default":1})";
constexpr const char* kB = R"("b:b":{"type":"int","default":2})";
constexpr const char* kC = R"("c:c":{"type":"int","default":3})";
constexpr const char* kBadC = R"("c:c":{"type":"int"})"; // missing default

std::string snapshot(const std::vector<const char*>& entries) {
	std::string json = R"({"schema_version":1,"version":1,"knobs":{)";
	for (std::size_t i = 0; i < entries.size(); ++i)
		json += (i ? "," : "") + std::string(entries[i]);
	return json + "}}";
}

std::string patch(const std::vector<const char*>& entries) {
	std::string json = R"({"schema_version":1,"version":2,"knobs":{)";
	for (std::size_t i = 0; i < entries.size(); ++i)
		json += (i ? "," : "") + std::string(entries[i]);
	return json + "}}";
}

std::string error_of(const Result<Snapshot, UpdateError>& r) {
	CHECK(!r.has_value());
	return r.error().detail;
}
std::string error_of(const Result<SnapshotPatch, UpdateError>& r) {
	CHECK(!r.has_value());
	return r.error().detail;
}

void knob_names() {
	const std::string dup_a = "duplicate knob name: a:a";
	CHECK(error_of(parse_source_snapshot(snapshot({kA, kB, kA, kBadC}), "s")) == dup_a);
	CHECK(error_of(parse_source_snapshot(snapshot({kA, kBadC, kA}), "s")) == "int knob missing required 'default': c:c");
	// Invalid name in place of the invalid definition.
	const char* bad_name = R"("nocolon":{"type":"int","default":1})";
	CHECK(error_of(parse_source_snapshot(snapshot({kA, kB, kA, bad_name}), "s")) == dup_a);
	CHECK(error_of(parse_source_snapshot(snapshot({kA, bad_name, kA}), "s")) == "invalid knob name: nocolon");
	// The second occurrence itself invalid: the invalid entry is reached first.
	CHECK(error_of(parse_source_snapshot(snapshot({kA, kB, R"("a:a":{"type":"int"})"}), "s")) ==
	      "int knob missing required 'default': a:a");
	// Two duplicate groups: the one whose SECOND occurrence comes first wins.
	CHECK(error_of(parse_source_snapshot(snapshot({kA, kB, kB, kA}), "s")) == "duplicate knob name: b:b");
	CHECK(error_of(parse_source_snapshot(snapshot({kB, kA, kA, kB}), "s")) == "duplicate knob name: a:a");
	// Triple repeat: still the key.
	CHECK(error_of(parse_source_snapshot(snapshot({kC, kA, kC, kC}), "s")) == "duplicate knob name: c:c");
	// No duplicate, any order: accepted and sorted.
	auto ok = parse_source_snapshot(snapshot({kC, kA, kB}), "s");
	CHECK(ok.has_value());
	std::vector<std::string> names;
	for (const auto& [k, v] : ok.value().knobs)
		names.push_back(k);
	CHECK((names == std::vector<std::string>{"a:a", "b:b", "c:c"}));
	// Same through the defaults entry point.
	CHECK(error_of(parse_defaults(R"({"schema_version":1,"knobs":{)" + std::string(kA) + "," + kB + "," + kA + "," + kBadC + "}}")) ==
	      dup_a);
}

void patch_names() {
	const char* null_c = R"("c:c":null)";
	const std::string dup_a = "duplicate knob name: a:a";
	CHECK(error_of(parse_snapshot_patch(patch({kA, kB, kA, kBadC}), "s")) == dup_a);
	CHECK(error_of(parse_snapshot_patch(patch({kA, kBadC, kA}), "s")) == "int knob missing required 'default': c:c");
	CHECK(error_of(parse_snapshot_patch(patch({kA, R"("a:a":null)", null_c}), "s")) == dup_a);
	auto ok = parse_snapshot_patch(patch({null_c, kA, kB}), "s");
	CHECK(ok.has_value());
	CHECK(ok.value().knobs.size() == 3 && ok.value().knobs.begin()->first == "a:a" &&
	      !ok.value().knobs.at("c:c").has_value());
}

std::string bool_knob(const std::string& overrides_body, const std::string& after = "") {
	return R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":false,"overrides":{)" + overrides_body + "}" +
	       after + "}}}";
}

void override_keys() {
	const std::string dup_g = "duplicate override key 'g' on knob a:b";
	const std::string bad_value = "override must be boolean or {rollout_pct: 0..100}: a:b";
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"h":false,"g":false,"i":7)"))) == dup_g);
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"i":7,"g":false)"))) == bad_value);
	// Bad group key instead of bad value.
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"h":false,"g":false,"":true)"))) == dup_g);
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"":true,"g":false)"))) == "bad group key on knob a:b");
	// An error in a LATER member of the knob object still loses to the
	// duplicate that precedes it.
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"g":false)", R"(,"rollout_pct":"x")"))) == dup_g);
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true,"h":true)", R"(,"rollout_pct":"x")"))) ==
	      "rollout_pct wrong type: a:b");
	// Two distinct duplicate groups: the earlier second occurrence wins.
	CHECK(error_of(parse_defaults(bool_knob(R"("h":true,"g":true,"g":false,"h":false)"))) == dup_g);
	// Duplicate across a repeated "overrides" member.
	CHECK(error_of(parse_defaults(bool_knob(R"("g":true)", R"(,"overrides":{"g":false})"))) == dup_g);
	// Any order, no duplicates: accepted, sorted, rollout objects kept.
	auto ok = parse_defaults(bool_knob(R"("h":{"rollout_pct":5},"g":true,"f":false)"));
	CHECK(ok.has_value());
	const auto& overrides = std::get<BoolDef>(ok.value().knobs.at("a:b")).overrides;
	CHECK(overrides.size() == 3 && overrides.begin()->first == "f" && std::prev(overrides.end())->first == "h");
	CHECK(std::get<GroupRollout>(overrides.at("h")).rollout_pct == 5);
	// A duplicate inside a later knob is reported with that knob's name, and a
	// duplicate knob name before it wins by input order.
	const std::string two = R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":true},)"
	                        R"("c:d":{"type":"bool","default":false,"overrides":{"g":true,"g":true}}}})";
	CHECK(error_of(parse_defaults(two)) == "duplicate override key 'g' on knob c:d");
	// Same override rules inside a patch.
	CHECK(error_of(parse_snapshot_patch(patch({R"("a:b":{"type":"bool","default":false,"overrides":{"g":true,"g":true}})"}), "s")) ==
	      dup_g);
}

// Random-order, duplicate-free input equals the sorted result; with a
// duplicate injected, the reported key is the first repeat in input order.
void random_orders() {
	std::mt19937 rng(11);
	for (int round = 0; round < 200; ++round) {
		const unsigned n = 2 + rng() % 30;
		std::vector<unsigned> ids(n);
		for (unsigned i = 0; i < n; ++i)
			ids[i] = i;
		std::shuffle(ids.begin(), ids.end(), rng);
		const bool inject = rng() % 2;
		std::vector<unsigned> seq = ids;
		std::string want_dup;
		if (inject) {
			const unsigned x = ids[rng() % n], y = ids[rng() % n];
			seq.insert(seq.begin() + rng() % (n + 1), x);
			seq.insert(seq.begin() + rng() % (n + 2), y);
			std::vector<unsigned> seen;
			for (unsigned id : seq) {
				if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
					want_dup = id < 10 ? "k:0" + std::to_string(id) : "k:" + std::to_string(id);
					break;
				}
				seen.push_back(id);
			}
		}
		std::vector<std::string> bodies;
		for (unsigned id : seq)
			bodies.push_back((id < 10 ? "\"k:0" : "\"k:") + std::to_string(id) + "\":{\"type\":\"int\",\"default\":" +
			                 std::to_string(id) + "}");
		std::vector<const char*> entries;
		for (const auto& b : bodies)
			entries.push_back(b.c_str());
		auto r = parse_source_snapshot(snapshot(entries), "s");
		if (inject && !want_dup.empty()) {
			CHECK(error_of(r) == "duplicate knob name: " + want_dup);
		} else {
			CHECK(r.has_value());
			CHECK(r.value().knobs.size() == n);
			CHECK(std::is_sorted(r.value().knobs.begin(), r.value().knobs.end(),
			                     [](const auto& a, const auto& b) { return a.first < b.first; }));
		}
	}
}

} // namespace

int main() {
	knob_names();
	patch_names();
	override_keys();
	random_orders();
	std::puts("PASS duplicate precedence");
	return 0;
}
