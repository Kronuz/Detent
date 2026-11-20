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
// W1: a consumer's place in one namespace, under one incarnation of it; position 0 is empty.
struct NamespaceCursor {
	Identity cluster{}, incarnation{};
	std::uint64_t position = 0;
	bool operator==(const NamespaceCursor &) const = default;
	bool same_identity(const NamespaceCursor &other) const noexcept {
		return cluster == other.cluster && incarnation == other.incarnation;
	}
};
// D5: a consumer's place in the namespace directory stream.
struct DirectoryCursor {
	Identity cluster{};
	std::uint64_t position = 0;
	bool operator==(const DirectoryCursor &) const = default;
};
// D88: a v1 cursor as a namespace cursor, until v2 sessions carry W1 cursors. The incarnation digests
// the cluster, group, restore epoch and scope, so a reset changing any of them is a new incarnation.
NamespaceCursor namespace_cursor(const Cursor &);
struct Profile {
	std::uint32_t semantic_id = 1;
	std::uint64_t required_capabilities = 1;
	bool operator==(const Profile &) const = default;
};
} // namespace detent::distribution
