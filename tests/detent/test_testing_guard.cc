// Test-only client: thread-local override for the global adapter,
// with guard-based nesting that prevents cross-test leakage.
#include <cassert>
#include <cstdio>

#include "detent/global.hh"
#include "detent/testing.hh"

using namespace detent;

namespace {

Snapshot make_snapshot(bool flag_value) {
	Snapshot snap;
	snap.version = 0;
	snap.source_id = "test";
	KnobDef def;
	def.default_value = flag_value ? 1 : 0;
	snap.knobs["team:flag"] = def;
	snap.knobs["team:count"] = KnobDef{2};
	return snap;
}

} // namespace

int main() {
	// Before any TestGuard is installed, the global adapter is Uninitialized.
	auto before = global::get_as<bool>("team:flag");
	assert(!before.has_value());
	assert(before.error().category == ErrorCategory::Uninitialized);

	{
		auto outer = testing::TestKnobs(make_snapshot(true)).set_thread_local();
		auto r1 = global::get_as<bool>("team:flag");
		assert(r1.has_value() && r1.value() == true);
		// A failed conversion through the global adapter is counted on the active client.
		const auto& metrics = global::detail::active_client()->metrics();
		const auto failed = metrics.reads_conversion_failed.load();
		auto wide = global::get_as<bool>("team:count");
		assert(!wide.has_value() && wide.error().category == ErrorCategory::ConversionFailed);
		assert(metrics.reads_conversion_failed.load() == failed + 1);

		{
			auto inner = testing::TestKnobs(make_snapshot(false)).set_thread_local();
			auto r2 = global::get_as<bool>("team:flag");
			assert(r2.has_value() && r2.value() == false);
		}
		// Dropping the inner guard restores the outer one.
		auto r3 = global::get_as<bool>("team:flag");
		assert(r3.has_value() && r3.value() == true);
	}

	// Dropping the outer guard restores Uninitialized.
	auto after = global::get_as<bool>("team:flag");
	assert(!after.has_value());
	assert(after.error().category == ErrorCategory::Uninitialized);

	std::puts("thread-local TestGuard nesting OK");
	return 0;
}
