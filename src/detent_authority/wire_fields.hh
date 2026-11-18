#pragma once
#include "detent/model.hh"
#include "detent_distribution/wire_encoder.hh"
namespace detent::authority::detail {
// The distribution wire tuple of an owned definition; its override rows follow.
template <class Output> void write_wire_definition_head(Output &out, const KnobDef &definition) {
	distribution::detail::wire::definition(
		out, distribution::WireDefinition{definition.default_value, {0, definition.overrides.size()},
										  definition.rollout_pct, definition.rollout_value});
}
template <class Output>
void write_wire_override(Output &out, std::string_view name, const GroupOverride &value) {
	distribution::detail::wire::override_row(out, name, value);
}
} // namespace detent::authority::detail
