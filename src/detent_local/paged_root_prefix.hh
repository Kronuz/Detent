#pragma once
#include "detent_local/paged_layout.hh"
#include <optional>
namespace detent::local::detail {
// Structural prefix checks supply no graph or segment deletion authority.
std::optional<std::pair<distribution::Cursor, distribution::Profile>>
	validate_paged_root_prefix(std::span<const char>);
paged::Limits paged_format_limits() noexcept;
} // namespace detent::local::detail
