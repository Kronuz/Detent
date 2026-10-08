#pragma once
#include "detent/model.hh"
#include "detent_distribution/messagepack.hh"
namespace detent::authority::detail {
template <class Output> void write_wire_definition_head(Output &out, const KnobDef &definition) {
	namespace m = distribution::messagepack;
	if (auto flag = std::get_if<BoolDef>(&definition)) {
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		out.push_back(flag->default_value ? '\xc3' : '\xc2');
		if (flag->rollout_pct)
			m::unsigned_integer(out, *flag->rollout_pct);
		else
			out.push_back('\xc0');
		m::array(out, flag->overrides.size());
	} else {
		m::array(out, 2);
		m::unsigned_integer(out, 1);
		m::signed_integer(out, std::get<IntDef>(definition).default_value);
	}
}
template <class Output>
void write_wire_override(Output &out, std::string_view name, const GroupOverride &value) {
	namespace m = distribution::messagepack;
	m::array(out, 3);
	m::string(out, name);
	if (auto forced = std::get_if<bool>(&value)) {
		m::unsigned_integer(out, 0);
		out.push_back(*forced ? '\xc3' : '\xc2');
	} else {
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, std::get<GroupRollout>(value).rollout_pct);
	}
}
} // namespace detent::authority::detail
