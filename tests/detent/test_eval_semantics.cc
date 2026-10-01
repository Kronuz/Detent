// Section 13 semantic matrix: bool default/group/rollout precedence, CLI
// overrides, missing/wrong-type errors, and checked int conversion. Uses the
// in-memory Client constructor (detent::testing's own plumbing) to avoid
// depending on a real defaults file for pure-evaluation semantics.
#include <cassert>
#include <cstdio>

#include "detent/client.hh"

using namespace detent;

namespace {

Snapshot make_snapshot() {
	Snapshot snap;
	snap.version = 0;
	snap.source_id = "test";

	BoolDef killswitch;
	killswitch.default_value = false;
	killswitch.overrides["employees"] = true;
	killswitch.rollout_pct = 10;
	snap.knobs["checkout:killswitch_new_path"] = killswitch;

	BoolDef plain;
	plain.default_value = true;
	snap.knobs["checkout:plain_flag"] = plain;

	IntDef max_results;
	max_results.default_value = 25;
	snap.knobs["search:max_results"] = max_results;

	return snap;
}

void test_bool_default_no_override_no_rollout() {
	Client c(make_snapshot());
	auto r = c.eval("checkout:plain_flag");
	assert(r.has_value() && r.value() == true);
	std::puts("bool default (no override/rollout) OK");
}

void test_group_match_and_precedence() {
	Client c(make_snapshot());
	// Group match wins over default.
	auto matched = c.eval("checkout:killswitch_new_path", std::nullopt, "employees");
	assert(matched.has_value() && matched.value() == true);

	// Group match ALSO wins over rollout, even when a hash is supplied
	// (Section 8: "Group precedence | Group match plus rollout | Group value
	// wins").
	auto precedence = c.eval("checkout:killswitch_new_path", "user-42", "employees");
	assert(precedence.has_value() && precedence.value() == true);

	// Unknown group key with no hash falls through to default.
	auto miss = c.eval("checkout:killswitch_new_path", std::nullopt, "unknown-group");
	assert(miss.has_value() && miss.value() == false);

	std::puts("group match + precedence OK");
}

void test_rollout_boundaries() {
	Client c(make_snapshot());

	// rollout_pct set but no hash_val: skip rollout, return default.
	auto no_hash = c.eval("checkout:killswitch_new_path");
	assert(no_hash.has_value() && no_hash.value() == false);

	Snapshot snap0 = make_snapshot();
	std::get<BoolDef>(snap0.knobs["checkout:killswitch_new_path"]).rollout_pct = 0;
	Client c0(snap0);
	assert(c0.eval("checkout:killswitch_new_path", "anybody").value() == false);

	Snapshot snap100 = make_snapshot();
	std::get<BoolDef>(snap100.knobs["checkout:killswitch_new_path"]).rollout_pct = 100;
	Client c100(snap100);
	assert(c100.eval("checkout:killswitch_new_path", "anybody").value() == true);

	std::puts("rollout boundaries (0/100/no-hash) OK");
}

void test_missing_and_wrong_type() {
	Client c(make_snapshot());

	auto missing = c.eval("checkout:does_not_exist");
	assert(!missing.has_value());
	assert(missing.error().category == ErrorCategory::MissingKnob);

	auto wrong_type_eval = c.eval("search:max_results"); // int knob, bool read
	assert(!wrong_type_eval.has_value());
	assert(wrong_type_eval.error().category == ErrorCategory::WrongType);

	auto wrong_type_get = c.get("checkout:plain_flag"); // bool knob, int read
	assert(!wrong_type_get.has_value());
	assert(wrong_type_get.error().category == ErrorCategory::WrongType);

	auto invalid_name = c.eval("nocolon");
	assert(!invalid_name.has_value());
	assert(invalid_name.error().category == ErrorCategory::InvalidName);

	std::puts("missing/wrong-type/invalid-name errors OK");
}

void test_get_and_checked_conversion() {
	Client c(make_snapshot());

	auto v = c.get("search:max_results");
	assert(v.has_value() && v.value() == 25);

	auto as_i8_ok = c.get_as<std::int8_t>("search:max_results");
	assert(as_i8_ok.has_value() && as_i8_ok.value() == 25);

	Snapshot big = make_snapshot();
	std::get<IntDef>(big.knobs["search:max_results"]).default_value = 1'000'000;
	Client c_big(big);
	auto as_i8_overflow = c_big.get_as<std::int8_t>("search:max_results");
	assert(!as_i8_overflow.has_value());
	assert(as_i8_overflow.error().category == ErrorCategory::ConversionFailed);
	auto failures_after_overflow = c_big.metrics().reads_conversion_failed.load();
	assert(failures_after_overflow >= 1); // was never incremented by get_as<T> before this fix

	// get_as<uint64_t>: the previous bounds check narrowed
	// numeric_limits<T>::max() into int64_t before comparing, which for an
	// unsigned target wraps to -1 and rejects every non-negative value --
	// confirmed broken: get_as<uint64_t> on the value 25 returned
	// ConversionFailed. std::in_range<T> fixes this for every target type.
	auto as_u64_ok = c.get_as<std::uint64_t>("search:max_results");
	assert(as_u64_ok.has_value() && as_u64_ok.value() == 25);

	std::puts("get + checked get_as<T> conversion (incl. unsigned targets) OK");
}

// Section 8's "Integer semantics": get(name, switch_val) accepts switch_val
// for API-shape compatibility with eval() but always ignores it, because
// the integer model has no per-group overrides in v1 -- unlike
// test_get_and_checked_conversion above (which never passes a switch_val
// at all), this proves the parameter is truly a no-op, not merely optional.
void test_get_ignores_switch_val() {
	Client c(make_snapshot());
	auto no_switch = c.get("search:max_results");
	auto with_a = c.get("search:max_results", "group-a");
	auto with_b = c.get("search:max_results", "totally-different-group");
	assert(no_switch.has_value() && with_a.has_value() && with_b.has_value());
	assert(no_switch.value() == with_a.value());
	assert(with_a.value() == with_b.value());
	std::puts("get() ignores switch_val (int model has no group overrides) OK");
}

// Section 8's stickiness guarantee ("across hosts, processes, languages, and
// restarts") exercised at the eval() level, not just stable_bucket()
// directly (test_hash_golden.cc already covers the raw hash function): two
// *independently constructed* Client instances, each with its own snapshot
// object and its own Metrics/override state -- the closest a single-process
// unit test can get to "two different processes" -- must agree on the same
// (knob_name, hash_val) pair.
void test_stickiness_across_independent_clients() {
	Client a(make_snapshot());
	Client b(make_snapshot());
	for (const char* hash_val : {"user-1", "user-2", "user-42", "tenant-abc", ""}) {
		auto ra = a.eval("checkout:killswitch_new_path", hash_val);
		auto rb = b.eval("checkout:killswitch_new_path", hash_val);
		assert(ra.has_value() && rb.has_value());
		assert(ra.value() == rb.value());
	}
	std::puts("eval() bucket stickiness across independent Client instances OK");
}

} // namespace

int main() {
	Snapshot fallback_snapshot;
	BoolDef fallback;
	fallback.default_value = true;
	fallback.rollout_pct = 100;
	fallback.overrides["none"] = GroupRollout{0};
	fallback_snapshot.knobs["a:b"] = fallback;
	Client fallback_client(std::move(fallback_snapshot));
	assert(fallback_client.eval("a:b", std::nullopt, "none").value());
	assert(!fallback_client.eval("a:b", "host", "none").value());
	assert(fallback_client.eval("a:b", "host", "unknown").value());
	test_bool_default_no_override_no_rollout();
	test_group_match_and_precedence();
	test_rollout_boundaries();
	test_missing_and_wrong_type();
	test_get_and_checked_conversion();
	test_get_ignores_switch_val();
	test_stickiness_across_independent_clients();
	return 0;
}
