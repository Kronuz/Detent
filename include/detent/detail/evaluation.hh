#pragma once
#include "detent/hashing.hh"
#include "detent/model.hh"
#include <optional>

namespace detent::detail {
// The one percentage rule for every evaluator: without a hash, the default; with one, the
// rollout value when stable_bucket(name, hash) < pct, otherwise the default.
template <class Name>
std::int64_t ramp(std::uint64_t pct, std::int64_t fallback, std::int64_t rollout_value, Name &&name,
				  std::optional<std::string_view> hash) {
	if (!hash)
		return fallback;
	return stable_bucket(name(), *hash) < pct ? rollout_value : fallback;
}
// A matching group override (forced value or percentage), then the knob-wide rollout, then the
// default.
inline std::int64_t evaluate(const KnobDef &definition, std::string_view name, std::optional<std::string_view> hash,
							 std::optional<std::string_view> group) {
	auto percentage = [&](std::uint64_t pct) {
		return ramp(pct, definition.default_value, definition.rollout_value.value_or(definition.default_value),
					[&] { return name; }, hash);
	};
	if (group) {
		auto override = definition.overrides.find(*group);
		if (override != definition.overrides.end()) {
			if (const auto *forced = std::get_if<std::int64_t>(&override->second))
				return *forced;
			return percentage(std::get<GroupRollout>(override->second).rollout_pct);
		}
	}
	return definition.rollout_pct ? percentage(*definition.rollout_pct) : definition.default_value;
}
} // namespace detent::detail
