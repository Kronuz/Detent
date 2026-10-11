#pragma once
#include "snapshot_fixture.hh"
inline std::string many(unsigned count) {
	std::string out;
	auto a = [&](std::uint32_t size) { m::array(out, size); };
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	const auto id = cursor();
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, {field->data(), field->size()}, true);
	m::string(out, {id.scope.data(), id.scope.size()}, true);
	n(id.revision);
	a(4);
	n(1);
	n(count);
	n(0);
	n(3 + 11ull * count);
	a(1);
	a(2);
	s("app");
	n(42);
	a(count);
	for (unsigned i = 0; i < count; ++i) {
		const auto digits = std::to_string(i);
		const auto name = "app:k" + std::string(6 - digits.size(), '0') + digits;
		a(4);
		n(0);
		s(name);
		n(1);
		a(2);
		n(1);
		m::signed_integer(out, i);
	}
	return out;
}
