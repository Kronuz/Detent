#pragma once
#include "detent/model.hh"
#include "detent_distribution/wire_codec.hh"
namespace detent::authority::detail {
namespace m = msgpack::bounded;
template <class Output> void write_wire_definition_head(Output &out, const KnobDef &definition) {
	m::AppendPacker pack(out);
	if (auto flag = std::get_if<BoolDef>(&definition)) {
		pack.pack_array(4);
		pack.pack_uint64(0);
		flag->default_value ? pack.pack_true() : pack.pack_false();
		if (flag->rollout_pct)
			pack.pack_uint64(*flag->rollout_pct);
		else
			pack.pack_nil();
		pack.pack_array(flag->overrides.size());
	} else {
		pack.pack_array(2);
		pack.pack_uint64(1);
		pack.pack_int64(std::get<IntDef>(definition).default_value);
	}
}
template <class Output>
void write_wire_override(Output &out, std::string_view name, const GroupOverride &value) {
	m::AppendPacker pack(out);
	pack.pack_array(3);
	pack.pack_str(name);
	if (auto forced = std::get_if<bool>(&value)) {
		pack.pack_uint64(0);
		*forced ? pack.pack_true() : pack.pack_false();
	} else {
		pack.pack_uint64(1);
		pack.pack_uint64(std::get<GroupRollout>(value).rollout_pct);
	}
}
} // namespace detent::authority::detail
