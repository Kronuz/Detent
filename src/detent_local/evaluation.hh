#pragma once
#include "borrowed_definition.hh"
#include "detent/detail/evaluation.hh"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <variant>

namespace detent::local {
// The evaluator over a borrowed definition, in detent::detail::evaluate's order and with its
// percentage rule (detail::ramp). `ns` and `key` name the knob (at most 198 and 198 bytes, 200
// with the colon) for the bucket.
inline std::int64_t evaluate(const detail::BorrowedDefinition &definition, std::string_view ns, std::string_view key,
							 std::optional<std::string_view> hash, std::optional<std::string_view> switch_value) {
	const auto fallback = definition.default_value();
	std::array<char, 200> qualified;
	auto name = [&] {
		auto stored = definition.qualified_key();
		if (!stored.empty())
			return stored;
		std::copy(ns.begin(), ns.end(), qualified.begin());
		qualified[ns.size()] = ':';
		std::copy(key.begin(), key.end(), qualified.begin() + ns.size() + 1);
		return std::string_view{qualified.data(), ns.size() + key.size() + 1};
	};
	auto percentage = [&](std::uint64_t pct) {
		return detent::detail::ramp(pct, fallback, definition.rollout_value(), name, hash);
	};
	if (switch_value) {
		if (const auto override = definition.override_for(*switch_value)) {
			if (const auto *rollout = std::get_if<GroupRollout>(&*override))
				return percentage(rollout->rollout_pct);
			return std::get<std::int64_t>(*override);
		}
	}
	const auto rollout = definition.rollout();
	return rollout ? percentage(*rollout) : fallback;
}
} // namespace detent::local
