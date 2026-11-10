#pragma once
#include "detent_local/paged_layout.hh"
#include <optional>
namespace detent::local::detail {
// Structural prefix checks supply no graph or segment deletion authority.
std::optional<std::pair<distribution::Cursor, distribution::Profile>>
	validate_paged_root_prefix(std::span<const char>);
inline paged::Limits paged_format_limits() noexcept {
	paged::Limits limits;
	limits.bytes = limits.pages = limits.segments = UINT64_MAX;
	limits.records = limits.namespaces = limits.prefix_bytes = UINT32_MAX;
	return limits;
}
} // namespace detent::local::detail
