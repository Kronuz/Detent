#include "detent_distribution/delta_encoder.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "storage_failure_fixture.hh"
#include <iostream>
namespace d = detent::distribution;
namespace m = d::messagepack;
void require(bool value) {
	if (!value)
		throw std::runtime_error("delta invariant failed");
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
void put_cursor(std::string &out, d::Cursor value) {
	m::array(out, 5);
	for (const auto *field : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(value.scope.data(), value.scope.size()), true);
	m::unsigned_integer(out, value.revision);
}
std::string fixture(bool progress = false, std::uint64_t base = 42, std::uint64_t target = 45,
					std::uint64_t transaction = 44, std::uint64_t strings = 20, bool duplicate = false,
					bool bad_delete = false, std::optional<detent::GroupOverride> group = {}) {
	std::string out;
	auto a = [&](std::uint32_t value) { m::array(out, value); };
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	a(progress ? 5 : 7);
	n(1);
	n(progress ? 3 : 2);
	a(2);
	n(1);
	n(1);
	auto identity = cursor();
	identity.revision = base;
	put_cursor(out, identity);
	identity.revision = target;
	put_cursor(out, identity);
	if (progress)
		return out;
	a(4);
	n(1);
	n(2);
	n(group ? 1 : 0);
	n(strings);
	a(1);
	a(3);
	n(transaction);
	a(1);
	a(2);
	s("app");
	n(transaction);
	a(2);
	a(4);
	n(0);
	s("app:count");
	n(1);
	out.push_back(bad_delete ? '\xc3' : '\xc0');
	a(4);
	n(0);
	s(duplicate ? "app:count" : "app:flag");
	n(0);
	a(4);
	n(0);
	out.push_back('\xc2');
	out.push_back('\xc0');
	a(group ? 1 : 0);
	if (group) {
		a(3);
		s("beta");
		if (const auto *forced = std::get_if<bool>(&*group)) {
			n(0);
			out.push_back(*forced ? '\xc3' : '\xc2');
		} else {
			n(1);
			n(std::get<detent::GroupRollout>(*group).rollout_pct);
		}
	}
	return out;
}
d::ValidatedDelta decode(std::string_view bytes, bool progress = false, d::Cursor expected = cursor()) {
	d::DeltaDecoder decoder(expected, {"app"}, {}, progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
	auto input = std::span(bytes.data(), bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
int main() {
	try {
		auto bytes = fixture();
		for (const auto &[expected, progress_only] : {
			std::pair{fixture(), false}, std::pair{fixture(true), true},
			std::pair{fixture(false, 42, 45, 44, 24, false, false, detent::GroupOverride(true)), false},
			std::pair{fixture(false, 42, 45, 44, 24, false, false, detent::GroupRollout{17}), false}}) {
			auto input = std::make_shared<const d::ValidatedDelta>(decode(expected, progress_only));
			const auto resident = input->storage_account()->used();
			{
				d::DeltaEncoder encoder(d::make_delta_source(input, 1));
				std::string encoded;
				unsigned turns = 0;
				while (!encoder.done()) {
					d::WorkBudget budget{1, 1};
					encoder.step(budget);
					if (encoder.chunk_ready())
						encoded.append(encoder.take_chunk()->view());
					require(++turns < 100000);
				}
				require(encoded == expected && encoder.encoded_length() == encoded.size());
				require(encoder.kind() == (progress_only ? d::UpdateKind::Progress : d::UpdateKind::Delta));
				for (;;) {
					d::WorkBudget budget{1, 1};
					if (encoder.discard(budget))
						break;
					require(++turns < 200000);
				}
			}
			require(input->storage_account()->used() == resident);
		}
		storage_failure::verify(
			bytes, [](d::Limits limits) { return d::DeltaDecoder(cursor(), {"app"}, limits); }, require);
		auto delta = decode(bytes);
		require(delta.base() == cursor() && delta.target().revision == 45 &&
				delta.transactions().size() == 1);
		auto &transaction = delta.transactions()[0];
		require(transaction.revision == 44 && delta.touched(transaction.touched.start).revision == 44 &&
				transaction.mutations.count == 2);
		require(!delta.mutation(transaction.mutations.start).value &&
				delta.mutation(transaction.mutations.start).key == "app:count");
		require(delta.mutation(transaction.mutations.start + 1).value &&
				!std::get<detent::distribution::WireBoolDef>(
					 *delta.mutation(transaction.mutations.start + 1).value)
					 .default_value);
		auto progress = decode(fixture(true), true);
		require(progress.kind() == d::UpdateKind::Progress && progress.transactions().empty() &&
				progress.target().revision == 45);
		for (auto malformed : {fixture(false, 41), fixture(false, 42, 42), fixture(false, 42, 45, 42),
							   fixture(false, 42, 45, 46), fixture(false, 42, 45, 44, 19),
							   fixture(false, 42, 45, 44, 21, true),
							   fixture(false, 42, 45, 44, 20, false, true), bytes + "\xc0"}) {
			bool rejected = false;
			try {
				decode(malformed);
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		for (std::size_t size = 0; size < bytes.size(); ++size) {
			bool rejected = false;
			try {
				decode(std::string_view(bytes).substr(0, size));
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		auto wrong = cursor();
		wrong.restore_epoch[0] = 'x';
		bool rejected = false;
		try {
			decode(bytes, false, wrong);
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		require(
			decode(fixture(true, 42, std::numeric_limits<std::uint64_t>::max()), true).target().revision ==
			std::numeric_limits<std::uint64_t>::max());
		std::size_t turns = 0;
		for (;;) {
			d::WorkBudget budget{1024, 1};
			++turns;
			if (delta.discard(budget))
				break;
		}
		require(turns >= 4 && delta.transactions().empty() && delta.base() == d::Cursor{});
		{
			d::DeltaDecoder pending(cursor(), {std::string(198, 'a')});
			d::WorkBudget zero{0, 1};
			require(!pending.discard(zero));
			bool done = false;
			for (unsigned i = 0; i < 1024 && !done; ++i) {
				d::WorkBudget tiny{1, 1};
				done = pending.discard(tiny);
			}
			require(done);
		}
		std::cout << "exact-base delta and progress decoding passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
