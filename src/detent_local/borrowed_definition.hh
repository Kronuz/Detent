#pragma once
#include "detent_local/paged_definition.hh"

namespace detent::local::detail {
// Accessors borrow only from an already validated immutable root.
struct BorrowedDefinition {
	std::span<const char> record, flat_image;
	const Layout *flat_layout = nullptr;
	paged::Definition paged_header{};   // decoded once by from_paged
	static BorrowedDefinition from_paged(std::span<const char> body);
	std::int64_t default_value() const;
	std::optional<std::uint8_t> rollout() const;
	std::int64_t rollout_value() const;   // the default when the definition has none
	std::string_view qualified_key() const;
	std::optional<GroupOverride> override_for(std::string_view key) const;
};
} // namespace detent::local::detail
