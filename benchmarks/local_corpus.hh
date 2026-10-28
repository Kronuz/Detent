#pragma once
#include "detent_distribution/wire_codec.hh"
#include "detent_distribution/protocol.hh"
#include <algorithm>
#include <stdexcept>
namespace detent::bench {
inline std::size_t local_key_width(std::uint32_t count) {
	if (!count)
		throw std::invalid_argument("local corpus requires records");
	return std::max<std::size_t>(6, std::to_string(count - 1).size());
}
inline std::string local_key(std::uint32_t index, std::size_t width) {
	const auto digits = std::to_string(index);
	if (digits.size() > width)
		throw std::invalid_argument("local corpus key width exceeded");
	return "key" + std::string(width - digits.size(), '0') + digits;
}
struct LocalCorpus {
	distribution::Cursor cursor;
	std::string wire;
	std::uint32_t integers, overrides;
};
inline LocalCorpus local_corpus(std::uint32_t count) {
	namespace d = distribution;
	namespace m = msgpack::bounded;
	const auto width = local_key_width(count);
	auto integers = count / 4 + (count % 4 != 0), overrides = count - integers;
	d::Cursor cursor;
	cursor.cluster.fill('c');
	cursor.group.fill('g');
	cursor.restore_epoch.fill('e');
	cursor.scope.fill('s');
	cursor.revision = 1;
	std::string wire;
	auto a = [&](auto n) { m::AppendPacker(wire).pack_array(n); };
	auto u = [&](auto n) { m::AppendPacker(wire).pack_uint64(n); };
	auto s = [&](std::string_view text) { m::AppendPacker(wire).pack_str(text); };
	a(7);
	u(1);
	u(1);
	a(2);
	u(1);
	u(1);
	a(5);
	for (auto *id : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
		m::AppendPacker(wire).pack_bin({id->data(), id->size()});
	m::AppendPacker(wire).pack_bin({cursor.scope.data(), cursor.scope.size()});
	u(1);
	a(4);
	u(1);
	u(count);
	u(overrides);
	u(3 + std::uint64_t(count) * (7 + width) + std::uint64_t(overrides) * 4);
	a(1);
	a(2);
	s("app");
	u(1);
	a(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		const auto key = "app:" + local_key(i, width);
		a(4);
		u(0);
		s(key);
		u(1);
		if (i % 4 == 0) {
			a(2);
			u(1);
			m::AppendPacker(wire).pack_int64(-std::int64_t(i));
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
