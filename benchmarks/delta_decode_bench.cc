#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include <chrono>
#include <iostream>
#include <sys/resource.h>
namespace d = detent::distribution;
namespace m = d::messagepack;
int main(int argc, char **argv) {
	if (argc != 2)
		return 1;
	auto count = std::stoull(argv[1]);
	if (!count || count > 500000)
		return 1;
	d::Cursor base;
	base.cluster.fill('c');
	base.group.fill('g');
	base.restore_epoch.fill('e');
	base.scope.fill('s');
	base.revision = 42;
	auto target = base;
	target.revision += count;
	std::string bytes;
	auto a = [&](std::uint32_t n) { m::array(bytes, n); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(bytes, v); };
	auto s = [&](std::string_view v) { m::string(bytes, v); };
	auto cursor = [&](d::Cursor value) {
		a(5);
		for (auto *field : {&value.cluster, &value.group, &value.restore_epoch})
			m::string(bytes, {field->data(), field->size()}, true);
		m::string(bytes, {value.scope.data(), value.scope.size()}, true);
		n(value.revision);
	};
	a(7);
	n(1);
	n(2);
	a(2);
	n(1);
	n(1);
	cursor(base);
	cursor(target);
	a(4);
	n(count);
	n(count);
	n(0);
	n(count * 12);
	a(count);
	for (std::uint64_t i = 0; i < count; ++i) {
		a(3);
		n(43 + i);
		a(1);
		a(2);
		s("app");
		n(43 + i);
		a(1);
		a(4);
		n(0);
		s("app:count");
		n(1);
		bytes.push_back('\xc0');
	}
	auto started = std::chrono::steady_clock::now();
	d::DeltaDecoder decoder(base, {"app"});
	auto input = std::span<const char>(bytes.data(), bytes.size());
	std::size_t turns = 0;
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
		++turns;
	}
	auto value = decoder.finish();
	if (value.transactions().size() != count)
		return 2;
	auto wall =
		std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
			.count();
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
	const auto rss = usage.ru_maxrss;
#else
	const auto rss = usage.ru_maxrss * 1024;
#endif
	std::cout << "{\"transactions\":" << count << ",\"wire_bytes\":" << bytes.size()
			  << ",\"decode_wall_us\":" << wall << ",\"turns\":" << turns << ",\"max_rss_bytes\":" << rss
			  << "}\n";
}
