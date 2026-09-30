// Section 5 test-only client: thread-local override for the global adapter,
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
	BoolDef def;
	def.default_value = flag_value;
	snap.knobs["team:flag"] = def;
	return snap;
}

} // namespace

int main() {
	// Before any TestGuard is installed, the global adapter is Uninitialized.
	auto before = global::eval("team:flag");
	assert(!before.has_value());
	assert(before.error().category == ErrorCategory::Uninitialized);

	{
		auto outer = testing::TestKnobs(make_snapshot(true)).set_thread_local();
		auto r1 = global::eval("team:flag");
		assert(r1.has_value() && r1.value() == true);

		{
			auto inner = testing::TestKnobs(make_snapshot(false)).set_thread_local();
			auto r2 = global::eval("team:flag");
			assert(r2.has_value() && r2.value() == false);
		}
		// Dropping the inner guard restores the outer one.
		auto r3 = global::eval("team:flag");
		assert(r3.has_value() && r3.value() == true);
	}

	// Dropping the outer guard restores Uninitialized.
	auto after = global::eval("team:flag");
	assert(!after.has_value());
	assert(after.error().category == ErrorCategory::Uninitialized);

	std::puts("thread-local TestGuard nesting OK");
	return 0;
}
