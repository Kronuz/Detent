#pragma once
#include "paged_disk_fixture.hh"
inline std::shared_ptr<const d::ValidatedDelta> progress(d::Cursor base,
														 std::shared_ptr<d::StorageLedger> account) {
	std::string out;
	m::AppendPacker(out).pack_array(5);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(3);
	m::AppendPacker(out).pack_array(2);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	auto encode = [&](const d::Cursor &c) {
		m::AppendPacker(out).pack_array(5);
		for (const auto *field : {&c.cluster, &c.group, &c.restore_epoch})
			m::AppendPacker(out).pack_bin({field->data(), field->size()});
		m::AppendPacker(out).pack_bin({c.scope.data(), c.scope.size()});
		m::AppendPacker(out).pack_uint64(c.revision);
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
