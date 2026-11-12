// Semantic matrix: default/group/rollout precedence, CLI overrides,
// missing-knob errors, checked get_as<T> conversion (bool included), and an
// integer knob with a rollout and group overrides. Uses the in-memory Client
// constructor (detent::testing's own plumbing) to avoid depending on a real
// defaults file for pure-evaluation semantics.
#include <cassert>
#include <cstdio>
#include <string>

#include "detent/client.hh"
#include "detent/hashing.hh"

using namespace detent;

namespace {

Snapshot make_snapshot() {
	Snapshot snap;
	snap.version = 0;
	snap.source_id = "test";

	KnobDef killswitch;
	killswitch.default_value = 0;
	killswitch.overrides["employees"] = std::int64_t{1};
	killswitch.rollout_pct = 10;
	killswitch.rollout_value = 1;
	snap.knobs["checkout:killswitch_new_path"] = killswitch;

	KnobDef plain;
	plain.default_value = 1;
	snap.knobs["checkout:plain_flag"] = plain;

	KnobDef max_results;
	max_results.default_value = 25;
	snap.knobs["search:max_results"] = max_results;

	// An integer knob with a rollout and both kinds of group override (A2).
	KnobDef batch;
	batch.default_value = 25;
	batch.rollout_pct = 10;
	batch.rollout_value = 50;
	batch.overrides["beta"] = std::int64_t{7};
	batch.overrides["canary"] = GroupRollout{50};
	snap.knobs["search:batch_size"] = batch;

	return snap;
}

void test_bool_default_no_override_no_rollout() {
	Client c(make_snapshot());
	auto r = c.get("checkout:plain_flag");
	assert(r.has_value() && r.value() == 1);
	auto b = c.get_as<bool>("checkout:plain_flag");
	assert(b.has_value() && b.value() == true);
	std::puts("bool default (no override/rollout) OK");
}

void test_group_match_and_precedence() {
	Client c(make_snapshot());
	// Group match wins over default.
	auto matched = c.get("checkout:killswitch_new_path", std::nullopt, "employees");
	assert(matched.has_value() && matched.value() == 1);

	// A group match also wins over the rollout, even when a hash is supplied.
	auto precedence = c.get("checkout:killswitch_new_path", "user-42", "employees");
	assert(precedence.has_value() && precedence.value() == 1);

	// Unknown group key with no hash falls through to default.
	auto miss = c.get("checkout:killswitch_new_path", std::nullopt, "unknown-group");
	assert(miss.has_value() && miss.value() == 0);

	std::puts("group match + precedence OK");
}

void test_rollout_boundaries() {
	Client c(make_snapshot());

	// rollout_pct set but no hash_val: skip rollout, return default.
	auto no_hash = c.get("checkout:killswitch_new_path");
	assert(no_hash.has_value() && no_hash.value() == 0);

	Snapshot snap0 = make_snapshot();
	snap0.knobs["checkout:killswitch_new_path"].rollout_pct = 0;
	Client c0(snap0);
	assert(c0.get("checkout:killswitch_new_path", "anybody").value() == 0);

	Snapshot snap100 = make_snapshot();
	snap100.knobs["checkout:killswitch_new_path"].rollout_pct = 100;
	Client c100(snap100);
	assert(c100.get("checkout:killswitch_new_path", "anybody").value() == 1);

	std::puts("rollout boundaries (0/100/no-hash) OK");
}

void test_missing_and_invalid_name() {
	Client c(make_snapshot());

	auto missing = c.get("checkout:does_not_exist");
	assert(!missing.has_value());
	assert(missing.error().category == ErrorCategory::MissingKnob);

	auto invalid_name = c.get("nocolon");
	assert(!invalid_name.has_value());
	assert(invalid_name.error().category == ErrorCategory::InvalidName);

	std::puts("missing/invalid-name errors OK");
}

void test_get_and_checked_conversion() {
	Client c(make_snapshot());

	auto v = c.get("search:max_results");
	assert(v.has_value() && v.value() == 25);

	auto as_i8_ok = c.get_as<std::int8_t>("search:max_results");
	assert(as_i8_ok.has_value() && as_i8_ok.value() == 25);

	Snapshot big = make_snapshot();
	big.knobs["search:max_results"].default_value = 1'000'000;
	Client c_big(big);
	auto as_i8_overflow = c_big.get_as<std::int8_t>("search:max_results");
	assert(!as_i8_overflow.has_value());
	assert(as_i8_overflow.error().category == ErrorCategory::ConversionFailed);
	assert(c_big.metrics().reads_conversion_failed.load() >= 1);

	// std::in_range<T> handles unsigned targets: 25 fits uint64_t.
	auto as_u64_ok = c.get_as<std::uint64_t>("search:max_results");
	assert(as_u64_ok.has_value() && as_u64_ok.value() == 25);

	std::puts("get + checked get_as<T> conversion (incl. unsigned targets) OK");
}

// get_as<bool> takes exactly 0 and 1; anything else is ConversionFailed and counted.
void test_get_as_bool() {
	Snapshot snap;
	for (std::int64_t value : {0, 1, 2, -1}) {
		KnobDef def;
		def.default_value = value;
		snap.knobs["flags:v" + std::to_string(value + 1)] = def;
	}
	Client c(std::move(snap));
	auto zero = c.get_as<bool>("flags:v1");
	assert(zero.has_value() && zero.value() == false);
	auto one = c.get_as<bool>("flags:v2");
	assert(one.has_value() && one.value() == true);
	const auto before = c.metrics().reads_conversion_failed.load();
	auto two = c.get_as<bool>("flags:v3");
	assert(!two.has_value() && two.error().category == ErrorCategory::ConversionFailed);
	assert(c.metrics().reads_conversion_failed.load() == before + 1);
	auto negative = c.get_as<bool>("flags:v0");
	assert(!negative.has_value() && negative.error().category == ErrorCategory::ConversionFailed);
	assert(c.metrics().reads_conversion_failed.load() == before + 2);
	std::puts("get_as<bool>: 0/1 convert, 2 and -1 are ConversionFailed OK");
}

// A2: an integer knob ramps its rollout value and honors group overrides.
void test_integer_rollout_and_groups() {
	Client c(make_snapshot());
	const std::string_view name = "search:batch_size";
	unsigned ramped = 0, canary = 0;
	for (unsigned i = 0; i != 400; ++i) {
		const auto hash = "user-" + std::to_string(i);
		const auto bucket = stable_bucket(name, hash);
		assert(c.get(name, hash).value() == (bucket < 10 ? 50 : 25));
		assert(c.get(name, hash, "unknown").value() == (bucket < 10 ? 50 : 25));
		assert(c.get(name, hash, "beta").value() == 7);
		assert(c.get(name, hash, "canary").value() == (bucket < 50 ? 50 : 25));
		ramped += bucket < 10;
		canary += bucket < 50;
	}
	assert(ramped > 0 && ramped < 400 && canary > ramped && canary < 400);
	assert(c.get(name).value() == 25);
	assert(c.get(name, std::nullopt, "canary").value() == 25);
	assert(c.get(name, std::nullopt, "beta").value() == 7);
	std::puts("integer rollout value + group forced/percentage OK");
}

// The stickiness guarantee ("across hosts, processes, languages, and
// restarts") exercised at the get() level, not just stable_bucket() directly
// (test_hash_golden.cc covers the raw hash function): two independently
// constructed Client instances must agree on the same (knob_name, hash_val).
void test_stickiness_across_independent_clients() {
	Client a(make_snapshot());
	Client b(make_snapshot());
	for (const char* hash_val : {"user-1", "user-2", "user-42", "tenant-abc", ""}) {
		auto ra = a.get("checkout:killswitch_new_path", hash_val);
		auto rb = b.get("checkout:killswitch_new_path", hash_val);
		assert(ra.has_value() && rb.has_value());
		assert(ra.value() == rb.value());
	}
	std::puts("get() bucket stickiness across independent Client instances OK");
}

} // namespace

int main() {
	// A group percentage answers the default without a hash, and a group that
	// matches nothing falls through to the knob-wide rollout. A hashed caller
	// outside a percentage gets the default (D68), so this default-1
	// knob's 0% group answers 1 for a hash.
	Snapshot fallback_snapshot;
	KnobDef fallback;
	fallback.default_value = 1;
	fallback.rollout_pct = 100;
	fallback.rollout_value = 1;
	fallback.overrides["none"] = GroupRollout{0};
	fallback_snapshot.knobs["a:b"] = fallback;
	Client fallback_client(std::move(fallback_snapshot));
	assert(fallback_client.get("a:b", std::nullopt, "none").value() == 1);
	assert(fallback_client.get("a:b", "host", "none").value() == 1);
	assert(fallback_client.get("a:b", "host", "unknown").value() == 1);
	test_bool_default_no_override_no_rollout();
	test_group_match_and_precedence();
	test_rollout_boundaries();
	test_missing_and_invalid_name();
	test_get_and_checked_conversion();
	test_get_as_bool();
	test_integer_rollout_and_groups();
	test_stickiness_across_independent_clients();
	return 0;
}
