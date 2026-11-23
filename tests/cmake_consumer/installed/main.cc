// Publishes knob values into a test store and reads them through the installed Reader.
#include <detent/reader.hh>
#include <detent_testing/store.hh>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv) {
	if (argc != 2)
		return 2;
	detent::testing::Store store(argv[1]);
	store.publish(R"({"schema_version":1,"knobs":{"app:count":{"default":7},"app:on":{"default":1}}})");
	detent::Reader reader(argv[1], ::getuid());
	const auto count = reader.get("app:count", -1);
	const bool on = reader.get_as<bool>("app:on", false);
	const auto missing = reader.lookup("app:absent", 3);
	store.publish(R"({"schema_version":1,"knobs":{"app:count":{"default":8},"app:on":{"default":1}}})");
	reader.refresh();
	const auto later = reader.get("app:count", -1);
	std::printf("count=%lld on=%d missing=%s later=%lld\n", static_cast<long long>(count), on,
	            detent_status_name(missing.status), static_cast<long long>(later));
	return count == 7 && on && missing.status == DETENT_MISSING && missing.value == 3 && later == 8 ? 0 : 1;
}
