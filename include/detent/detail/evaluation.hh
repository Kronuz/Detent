#pragma once
#include "detent/errors.hh"
#include "detent/hashing.hh"
#include "detent/model.hh"
#include "detent/overrides.hh"
#include <optional>

namespace detent::detail {
inline bool evaluate(const BoolDef &definition, std::string_view name, std::optional<std::string_view> hash,
					 std::optional<std::string_view> group) {
	if (group) {
		auto override = definition.overrides.find(*group);
		if (override != definition.overrides.end()) {
			if (const auto *forced = std::get_if<bool>(&override->second))
				return *forced;
			return hash ? stable_bucket(name, *hash) < std::get<GroupRollout>(override->second).rollout_pct
						: definition.default_value;
		}
	}
	return definition.rollout_pct && hash ? stable_bucket(name, *hash) < *definition.rollout_pct
										  : definition.default_value;
}
template <class T>
std::optional<Result<T, KnobError>> override_value(const OverrideTable *table, std::string_view name) {
	if (!table)
		return {};
	auto found = table->values.find(name);
	if (found == table->values.end())
		return {};
	if (auto value = std::get_if<T>(&found->second))
		return Result<T, KnobError>(*value);
	return Result<T, KnobError>(
		KnobError{ErrorCategory::WrongType, std::string(name), "override has wrong type"});
}
} // namespace detent::detail
