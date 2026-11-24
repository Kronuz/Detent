// Publishes knob values into a test store, an entry per namespace, and reads them through the installed Reader.
#include <detent/reader.hh>
#include <detent_testing/store.hh>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>
#include <utility>

int main(int argc, char **argv) {
	if (argc != 2)
		return 2;
	detent::testing::Store store(argv[1]);
	store.publish(R"({"schema_version":1,"knobs":{"app:count":{"default":7},"app:on":{"default":1},"web:size":{"default":5}}})");
	detent::Reader reader(argv[1], ::getuid());
	const auto count = reader.get("app:count", -1);
	const bool on = reader.get_as<bool>("app:on", false);
	const auto size = reader.get("web:size", -1);
	const auto missing = reader.lookup("app:absent", 3);
	detent::testing::Store moved(std::move(store));
	bool refused = false;
	try {
		store.publish(R"({"schema_version":1,"knobs":{}})");
	} catch (const std::logic_error &) {
		refused = true;
	}
	moved.publish(R"({"schema_version":1,"knobs":{"app:count":{"default":8},"app:on":{"default":1},"web:size":{"default":5}}})");
	reader.refresh();
	const auto later = reader.get("app:count", -1);
	std::printf("count=%lld on=%d size=%lld missing=%s later=%lld refused=%d\n", static_cast<long long>(count), on,
	            static_cast<long long>(size), detent_status_name(missing.status), static_cast<long long>(later), refused);
	return count == 7 && on && size == 5 && missing.status == DETENT_MISSING && missing.value == 3 && later == 8 &&
	               refused
	           ? 0
	           : 1;
}
