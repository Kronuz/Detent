#pragma once
#include "borrowed_definition.hh"
#include "detent/hashing.hh"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <variant>

namespace detent::local {
// The one evaluator over a borrowed bool definition: a matching switch override (a forced bool or a
// group rollout), then the global rollout, then the default. Without a hash the default answers.
// `ns` and `key` name the knob (at most 198 and 198 bytes, 200 with the colon) for the bucket.
inline bool evaluate(const detail::BorrowedDefinition &definition, std::string_view ns, std::string_view key,
					 std::optional<std::string_view> hash, std::optional<std::string_view> switch_value) {
	const auto fallback = definition.default_bool();
	auto ramp = [&](std::uint64_t pct) {
		if (!hash)
			return fallback;
		auto name = definition.qualified_key();
		std::array<char, 200> qualified;
		if (name.empty()) {
			std::copy(ns.begin(), ns.end(), qualified.begin());
			qualified[ns.size()] = ':';
			std::copy(key.begin(), key.end(), qualified.begin() + ns.size() + 1);
			name = {qualified.data(), ns.size() + key.size() + 1};
		}
		return stable_bucket(name, *hash) < pct;
	};
	if (switch_value) {
		if (const auto override = definition.override_for(*switch_value)) {
			if (const auto *rollout = std::get_if<GroupRollout>(&*override))
				return ramp(rollout->rollout_pct);
			return std::get<bool>(*override);
		}
	}
	const auto rollout = definition.rollout();
	return rollout ? ramp(*rollout) : fallback;
}
} // namespace detent::local
