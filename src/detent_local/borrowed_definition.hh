#pragma once
#include "detent_local/paged_definition.hh"

namespace detent::local::detail {
// Accessors borrow only from an already validated immutable root.
struct BorrowedDefinition {
	std::span<const char> record, flat_image;
	const Layout *flat_layout = nullptr;
	static BorrowedDefinition from_paged(std::span<const char> body);
	bool boolean() const;
	bool default_bool() const;
	std::int64_t default_int() const;
	std::optional<std::uint8_t> rollout() const;
	std::string_view qualified_key() const;
	std::optional<GroupOverride> override_for(std::string_view key) const;
};
} // namespace detent::local::detail
