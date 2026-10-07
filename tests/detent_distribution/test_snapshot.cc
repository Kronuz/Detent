#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "storage_failure_fixture.hh"
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
int main() {
	try {
		auto bytes = fixture();
		storage_failure::verify(
			bytes, [](d::Limits limits) { return d::SnapshotDecoder(cursor(), {"app"}, limits); }, require);
		auto snapshot = decode(bytes);
		require(snapshot.cursor() == cursor() && snapshot.records().size() == 2 &&
				snapshot.namespaces().size() == 1);
		require(std::get<detent::IntDef>(snapshot.records()[0].definition).default_value ==
				std::numeric_limits<std::int64_t>::min());
		auto &flag = std::get<detent::distribution::WireBoolDef>(snapshot.records()[1].definition);
		require(flag.default_value && flag.rollout_pct == 25 &&
				std::get<detent::GroupRollout>(snapshot.overrides(flag).at("beta")).rollout_pct == 17);
		for (std::size_t length = 0; length < bytes.size(); ++length) {
			bool rejected = false;
			try {
				decode(std::string_view(bytes).substr(0, length));
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		for (auto malformed : {fixture(2), fixture(1, 23), fixture(1, 25), fixture(1, 24, 101),
							   fixture(1, 24, 25, 41), bytes + "\xc0"}) {
			bool rejected = false;
			try {
				decode(malformed);
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		auto wrong = cursor();
		wrong.scope[0] = 'x';
		bool rejected = false;
		try {
			decode(bytes, wrong);
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		auto maximum = cursor();
		maximum.revision = std::numeric_limits<std::uint64_t>::max();
		require(decode(fixture(1, 24, 25, maximum.revision), maximum).cursor().revision == maximum.revision);
		std::size_t disposal_turns = 0;
		for (;;) {
			d::WorkBudget budget{1024, 1};
			++disposal_turns;
			if (snapshot.discard(budget))
				break;
		}
		require(disposal_turns >= 4 && snapshot.records().empty() && snapshot.cursor() == d::Cursor{});
		d::SnapshotDecoder cancelled(cursor(), {"app"});
		auto partial = std::span<const char>(bytes.data(), bytes.size() - 1);
		while (!partial.empty()) {
			d::WorkBudget budget;
			cancelled.feed(partial, budget);
		}
		for (;;) {
			d::WorkBudget budget{1024, 1};
			if (cancelled.discard(budget))
				break;
		}
		rejected = false;
		try {
			cancelled.finish();
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		{
			d::SnapshotDecoder pending(cursor(), {std::string(198, 'a')});
			d::WorkBudget zero{0, 1};
			require(!pending.discard(zero));
			bool done = false;
			for (unsigned i = 0; i < 1024 && !done; ++i) {
				d::WorkBudget tiny{1, 1};
				done = pending.discard(tiny);
			}
			require(done);
		}
		std::cout << "bounded snapshot decoding passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
