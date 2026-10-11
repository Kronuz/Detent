#pragma once
#include "snapshot_fixture.hh"
inline void put_cursor(std::string &out, d::Cursor c) {
	m::array(out, 5);
	for (auto *id : {&c.cluster, &c.group, &c.restore_epoch})
		m::string(out, {id->data(), id->size()}, true);
	m::string(out, {c.scope.data(), c.scope.size()}, true);
	m::unsigned_integer(out, c.revision);
}
inline std::shared_ptr<const d::ValidatedDelta> delta(bool progress, unsigned transactions = 2,
													  std::string_view scope = "app") {
	std::string out;
	auto a = [&](unsigned n) { m::array(out, n); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(out, v); };
	auto s = [&](std::string_view v) { m::string(out, v); };
	a(progress ? 5 : 7);
	n(1);
	n(progress ? 3 : 2);
	a(2);
	n(1);
	n(1);
	put_cursor(out, cursor());
	auto target = cursor();
	target.revision = 43 + transactions;
	put_cursor(out, target);
	if (!progress) {
		a(4);
		n(transactions);
		n(transactions * 2);
		n(0);
		n(transactions * 20);
		a(transactions);
		for (unsigned tx = 0; tx < transactions; ++tx) {
			a(3);
			n(43 + tx);
			a(1);
			a(2);
			s(scope);
			n(43 + tx);
			a(2);
			a(4);
			n(0);
			s(std::string(scope) + ":count");
			n(tx == 0 ? 1 : 0);
			if (!tx)
				out.push_back('\xc0');
			else {
				a(2);
				n(1);
				m::signed_integer(out, tx);
			}
			a(4);
			n(0);
			s(std::string(scope) + ":flag");
			n(0);
			a(4);
			n(0);
			out.push_back(tx % 2 ? '\xc3' : '\xc2');
			out.push_back('\xc0');
			a(0);
		}
	}
	d::DeltaDecoder decoder(cursor(), {std::string(scope)}, {},
							progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
	auto input = std::span<const char>(out.data(), out.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return std::make_shared<const d::ValidatedDelta>(decoder.finish());
}
// Independent snapshot oracle: spell the expected final tuple stream directly.
inline std::shared_ptr<const d::ValidatedSnapshot> expected(bool progress, unsigned tx) {
	auto target = cursor();
	target.revision = 43 + tx;
	if (progress)
		return std::make_shared<const d::ValidatedSnapshot>(
			decode(fixture(1, 24, 25, target.revision), target));
	std::string out;
	auto a = [&](unsigned v) { m::array(out, v); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(out, v); };
	auto text = [&](std::string_view v) { m::string(out, v); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	put_cursor(out, target);
	a(4);
	n(1);
	n(tx == 1 ? 1 : 2);
	n(0);
	n(tx == 1 ? 11 : 20);
	a(1);
	a(2);
	text("app");
	n(42 + tx);
	a(tx == 1 ? 1 : 2);
	if (tx > 1) {
		a(4);
		n(0);
		text("app:count");
		n(42 + tx);
		a(2);
		n(1);
		m::signed_integer(out, tx - 1);
	}
	a(4);
	n(0);
	text("app:flag");
	n(42 + tx);
	a(4);
	n(0);
	out.push_back((tx - 1) % 2 ? '\xc3' : '\xc2');
	out.push_back('\xc0');
	a(0);
	return std::make_shared<const d::ValidatedSnapshot>(decode(out, target));
}
