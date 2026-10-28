#pragma once
#include "snapshot_fixture.hh"
inline void put_cursor(std::string &out, d::Cursor c) {
	m::AppendPacker(out).pack_array(5);
	for (auto *id : {&c.cluster, &c.group, &c.restore_epoch})
		m::AppendPacker(out).pack_bin({id->data(), id->size()});
	m::AppendPacker(out).pack_bin({c.scope.data(), c.scope.size()});
	m::AppendPacker(out).pack_uint64(c.revision);
}
inline std::string delta_wire(bool progress, unsigned transactions = 2, std::string_view scope = "app") {
	std::string out;
	auto a = [&](unsigned n) { m::AppendPacker(out).pack_array(n); };
	auto n = [&](std::uint64_t v) { m::AppendPacker(out).pack_uint64(v); };
	auto s = [&](std::string_view v) { m::AppendPacker(out).pack_str(v); };
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
				m::AppendPacker(out).pack_int64(tx);
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
	return out;
}
inline d::ValidatedDelta parse_delta(bool progress, unsigned transactions = 2, std::string_view scope = "app",
									 std::shared_ptr<d::StorageLedger> account = {}) {
	const auto out = delta_wire(progress, transactions, scope);
	d::Limits limits;
	limits.storage_account = std::move(account);
	d::DeltaDecoder decoder(cursor(), {std::string(scope)}, limits,
							progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
	auto input = std::span<const char>(out.data(), out.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
inline std::shared_ptr<const d::ValidatedDelta> delta(bool progress, unsigned transactions = 2,
													  std::string_view scope = "app") {
	return std::make_shared<const d::ValidatedDelta>(parse_delta(progress, transactions, scope));
}
// Independent snapshot oracle: spell the expected final tuple stream directly.
inline std::shared_ptr<const d::ValidatedSnapshot> expected(bool progress, unsigned tx) {
	auto target = cursor();
	target.revision = 43 + tx;
	if (progress)
		return std::make_shared<const d::ValidatedSnapshot>(
			decode(fixture(1, 24, 25, target.revision), target));
	std::string out;
	auto a = [&](unsigned v) { m::AppendPacker(out).pack_array(v); };
	auto n = [&](std::uint64_t v) { m::AppendPacker(out).pack_uint64(v); };
	auto text = [&](std::string_view v) { m::AppendPacker(out).pack_str(v); };
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
		m::AppendPacker(out).pack_int64(tx - 1);
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

inline d::ValidatedDelta one_key_delta(std::shared_ptr<d::StorageLedger> account) {
	std::string wire;
	auto a = [&](unsigned value) { m::AppendPacker(wire).pack_array(value); };
	auto n = [&](unsigned value) { m::AppendPacker(wire).pack_uint64(value); };
	a(7);
	n(1);
	n(2);
	a(2);
	n(1);
	n(1);
	put_cursor(wire, cursor());
	auto target = cursor();
	target.revision = 44;
	put_cursor(wire, target);
	a(4);
	n(1);
	n(1);
	n(0);
	n(12);
	a(1);
	a(3);
	n(43);
	a(1);
	a(2);
	m::AppendPacker(wire).pack_str("app");
	n(43);
	a(1);
	a(4);
	n(0);
	m::AppendPacker(wire).pack_str("app:count");
	n(0);
	a(2);
	n(1);
	n(1);
	d::Limits limits;
	limits.storage_account = std::move(account);
	d::DeltaDecoder decoder(cursor(), {"app"}, limits);
	auto bytes = std::span<const char>(wire);
	while (!bytes.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(bytes, budget);
	}
	return decoder.finish();
}
