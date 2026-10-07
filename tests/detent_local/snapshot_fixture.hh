#include "detent/hashing.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "detent_local/builder.hh"
#pragma once
#include <iostream>
namespace d = detent::distribution;
namespace m = d::messagepack;
void require(bool value) {
	if (!value)
		throw std::runtime_error("snapshot invariant failed");
}
d::Cursor cursor() {
	d::Cursor value;
	value.cluster.fill('c');
	value.group.fill('g');
	value.restore_epoch.fill('e');
	value.scope.fill('s');
	value.revision = 42;
	return value;
}
std::string fixture(std::uint64_t capabilities = 1, std::uint64_t strings = 24, std::uint64_t percentage = 25,
					std::uint64_t revision = 42) {
	std::string out;
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto a = [&](std::uint32_t value) { m::array(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(capabilities);
	a(5);
	const auto id = cursor();
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(id.scope.data(), id.scope.size()), true);
	n(revision);
	a(4);
	n(1);
	n(2);
	n(1);
	n(strings);
	a(1);
	a(2);
	s("app");
	n(42);
	a(2);
	a(4);
	n(0);
	s("app:count");
	n(41);
	a(2);
	n(1);
	m::signed_integer(out, std::numeric_limits<std::int64_t>::min());
	a(4);
	n(0);
	s("app:flag");
	n(42);
	a(4);
	n(0);
	out.push_back('\xc3');
	n(percentage);
	a(1);
	a(3);
	s("beta");
	n(1);
	n(17);
	return out;
}
d::ValidatedSnapshot decode(std::string_view bytes, d::Cursor expected = cursor()) {
	d::SnapshotDecoder decoder(expected, {"app"});
	auto input = std::span(bytes.data(), bytes.size());
	d::WorkBudget zero{0, 0};
	decoder.feed(input, zero);
	require(input.size() == bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
