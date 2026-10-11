#pragma once
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include <charconv>
namespace detent::bench {
struct LocalCorpus {
	distribution::Cursor cursor;
	std::string wire;
	std::uint32_t integers, overrides;
};
inline LocalCorpus local_corpus(std::uint32_t count) {
	namespace d = distribution;
	namespace m = d::messagepack;
	auto integers = (count + 3) / 4, overrides = count - integers;
	d::Cursor cursor;
	cursor.cluster.fill('c');
	cursor.group.fill('g');
	cursor.restore_epoch.fill('e');
	cursor.scope.fill('s');
	cursor.revision = 1;
	std::string wire;
	auto a = [&](auto n) { m::array(wire, n); };
	auto u = [&](auto n) { m::unsigned_integer(wire, n); };
	auto s = [&](std::string_view text) { m::string(wire, text); };
	a(7);
	u(1);
	u(1);
	a(2);
	u(1);
	u(1);
	a(5);
	for (auto *id : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
		m::string(wire, {id->data(), id->size()}, true);
	m::string(wire, {cursor.scope.data(), cursor.scope.size()}, true);
	u(1);
	a(4);
	u(1);
	u(count);
	u(overrides);
	u(3 + std::uint64_t(count) * 13 + std::uint64_t(overrides) * 4);
	a(1);
	a(2);
	s("app");
	u(1);
	a(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		char digits[7];
		auto end = std::to_chars(digits, digits + 7, i).ptr;
		std::string key = "app:key";
		key.append(6 - (end - digits), '0');
		key.append(digits, end);
		a(4);
		u(0);
		s(key);
		u(1);
		if (i % 4 == 0) {
			a(2);
			u(1);
			m::signed_integer(wire, -std::int64_t(i));
		} else {
			a(4);
			u(0);
			wire.push_back(i % 2 ? '\xc3' : '\xc2');
			u(25);
			a(1);
			a(3);
			s("beta");
			u(1);
			u(17);
		}
	}

	return {cursor, std::move(wire), integers, overrides};
}
} // namespace detent::bench
