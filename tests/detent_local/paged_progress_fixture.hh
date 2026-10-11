#pragma once
#include "paged_disk_fixture.hh"
inline std::shared_ptr<const d::ValidatedDelta> progress(d::Cursor base,
														 std::shared_ptr<d::StorageLedger> account) {
	std::string out;
	m::array(out, 5);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 3);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	auto encode = [&](const d::Cursor &c) {
		m::array(out, 5);
		for (const auto *field : {&c.cluster, &c.group, &c.restore_epoch})
			m::string(out, {field->data(), field->size()}, true);
		m::string(out, {c.scope.data(), c.scope.size()}, true);
		m::unsigned_integer(out, c.revision);
	};
	encode(base);
	auto target = base;
	target.revision += 3;
	encode(target);
	d::Limits limits;
	limits.storage_account = account;
	d::DeltaDecoder decoder(base, {"app"}, limits, d::UpdateKind::Progress);
	auto input = std::span<const char>(out.data(), out.size());
	while (!input.empty()) {
		d::WorkBudget b{1, 1};
		decoder.feed(input, b);
	}
	return std::make_shared<const d::ValidatedDelta>(decoder.finish());
}
