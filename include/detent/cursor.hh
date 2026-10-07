#pragma once
#include <array>
#include <cstdint>

namespace detent::distribution {
using Identity = std::array<char, 16>;
using ScopeIdentity = std::array<char, 32>;
struct Cursor {
	Identity cluster{}, group{}, restore_epoch{};
	ScopeIdentity scope{};
	std::uint64_t revision = 0;
	bool operator==(const Cursor &) const = default;
	bool same_identity(const Cursor &other) const noexcept {
		return cluster == other.cluster && group == other.group && restore_epoch == other.restore_epoch &&
			   scope == other.scope;
	}
};
struct Profile {
	std::uint32_t semantic_id = 1;
	std::uint64_t required_capabilities = 1;
	bool operator==(const Profile &) const = default;
};
} // namespace detent::distribution
