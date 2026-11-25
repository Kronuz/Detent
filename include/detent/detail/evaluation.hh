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
} // namespace detent::detail
