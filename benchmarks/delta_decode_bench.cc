#include "detent_distribution/wire_codec.hh"
#include "detent_distribution/protocol.hh"
#include "storage_metrics.hh"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sys/resource.h>
namespace d = detent::distribution;
namespace m = msgpack::bounded;
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
	auto a = [&](std::uint32_t n) { m::AppendPacker(bytes).pack_array(n); };
	auto n = [&](std::uint64_t v) { m::AppendPacker(bytes).pack_uint64(v); };
	auto s = [&](std::string_view v) { m::AppendPacker(bytes).pack_str(v); };
	auto cursor = [&](d::Cursor value) {
		a(5);
		for (auto *field : {&value.cluster, &value.group, &value.restore_epoch})
			m::AppendPacker(bytes).pack_bin({field->data(), field->size()});
		m::AppendPacker(bytes).pack_bin({value.scope.data(), value.scope.size()});
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
	auto charged = charged_storage(value);
	std::size_t retirement_turns = 0;
	std::int64_t maximum_retirement_turn = 0;
	auto retirement_start = std::chrono::steady_clock::now();
	for (;;) {
		d::WorkBudget budget;
		auto before = std::chrono::steady_clock::now();
		bool done = value.discard(budget);
		auto elapsed =
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - before)
				.count();
		maximum_retirement_turn = std::max<std::int64_t>(maximum_retirement_turn, elapsed);
		++retirement_turns;
		if (done)
			break;
	}
	auto retirement_us = std::chrono::duration_cast<std::chrono::microseconds>(
							 std::chrono::steady_clock::now() - retirement_start)
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
			  << ",\"charged_decode_capacity\":" << charged << ",\"retirement_us\":" << retirement_us
			  << ",\"retirement_turns\":" << retirement_turns
			  << ",\"max_retirement_turn_us\":" << maximum_retirement_turn
			  << ",\"retained_decode_capacity\":" << charged_storage(value) << "}\n";
}
