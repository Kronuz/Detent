#pragma once

// Section 8: stable hash for percentage rollout. Normative for this clone --
// never described as Meta's algorithm (see spec Section 2/8). Do not replace
// SHA-256 with a language runtime's default hash: many are randomized per
// process, which would break cohort stickiness across restarts and hosts.

#include <cstdint>
#include <string_view>

namespace detent {

// bucket = big-endian-u64(SHA256(name || 0x00 || hash_val))[0..8] % 100
// Returns a value in [0, 100).
[[nodiscard]] std::uint64_t stable_bucket(std::string_view knob_name, std::string_view hash_val);

} // namespace detent
