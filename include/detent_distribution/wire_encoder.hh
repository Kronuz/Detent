#pragma once
#include "definition.hh"
#include "wire_codec.hh"

namespace detent::distribution::detail::wire {
namespace m = msgpack::bounded;
template <class Bytes> void definition(Bytes &out, const WireDefinition &value) {
	m::AppendPacker pack(out);
	if (const auto *flag = std::get_if<WireBoolDef>(&value)) {
		pack.pack_array(4);
		pack.pack_uint64(0);
		flag->default_value ? pack.pack_true() : pack.pack_false();
		if (flag->rollout_pct)
			pack.pack_uint64(*flag->rollout_pct);
		else
			pack.pack_nil();
		pack.pack_array(flag->overrides.count);
	} else {
		pack.pack_array(2);
		pack.pack_uint64(1);
		pack.pack_int64(std::get<IntDef>(value).default_value);
	}
}
template <class Bytes> void override_row(Bytes &out, std::string_view name, const GroupOverride &value) {
	m::AppendPacker pack(out);
	pack.pack_array(3);
	pack.pack_str(name);
	if (const auto *forced = std::get_if<bool>(&value)) {
		pack.pack_uint64(0);
		*forced ? pack.pack_true() : pack.pack_false();
	} else {
		pack.pack_uint64(1);
		pack.pack_uint64(std::get<GroupRollout>(value).rollout_pct);
	}
}
} // namespace detent::distribution::detail::wire
