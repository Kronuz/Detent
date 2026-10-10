#pragma once
#include "definition.hh"
#include "messagepack.hh"

namespace detent::distribution::detail::wire {
template <class Bytes> void definition(Bytes &out, const WireDefinition &value) {
	if (const auto *flag = std::get_if<WireBoolDef>(&value)) {
		messagepack::array(out, 4);
		messagepack::unsigned_integer(out, 0);
		out.push_back(flag->default_value ? '\xc3' : '\xc2');
		if (flag->rollout_pct)
			messagepack::unsigned_integer(out, *flag->rollout_pct);
		else
			out.push_back('\xc0');
		messagepack::array(out, flag->overrides.count);
	} else {
		messagepack::array(out, 2);
		messagepack::unsigned_integer(out, 1);
		messagepack::signed_integer(out, std::get<IntDef>(value).default_value);
	}
}
template <class Bytes> void override_row(Bytes &out, std::string_view name, const GroupOverride &value) {
	messagepack::array(out, 3);
	messagepack::string(out, name);
	if (const auto *forced = std::get_if<bool>(&value)) {
		messagepack::unsigned_integer(out, 0);
		out.push_back(*forced ? '\xc3' : '\xc2');
	} else {
		messagepack::unsigned_integer(out, 1);
		messagepack::unsigned_integer(out, std::get<GroupRollout>(value).rollout_pct);
	}
}
} // namespace detent::distribution::detail::wire
