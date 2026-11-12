#pragma once
#include "definition.hh"
#include "messagepack.hh"

namespace detent::distribution::detail::wire {
// [default, rollout_pct | nil, rollout_value | nil, [override rows...]]; the rows follow.
template <class Bytes> void definition(Bytes &out, const WireDefinition &value) {
	messagepack::array(out, 4);
	messagepack::signed_integer(out, value.default_value);
	if (value.rollout_pct)
		messagepack::unsigned_integer(out, *value.rollout_pct);
	else
		out.push_back('\xc0');
	if (value.rollout_value)
		messagepack::signed_integer(out, *value.rollout_value);
	else
		out.push_back('\xc0');
	messagepack::array(out, value.overrides.count);
}
// [switch, 0, forced value] or [switch, 1, percentage].
template <class Bytes> void override_row(Bytes &out, std::string_view name, const GroupOverride &value) {
	messagepack::array(out, 3);
	messagepack::string(out, name);
	if (const auto *forced = std::get_if<std::int64_t>(&value)) {
		messagepack::unsigned_integer(out, 0);
		messagepack::signed_integer(out, *forced);
	} else {
		messagepack::unsigned_integer(out, 1);
		messagepack::unsigned_integer(out, std::get<GroupRollout>(value).rollout_pct);
	}
}
} // namespace detent::distribution::detail::wire
