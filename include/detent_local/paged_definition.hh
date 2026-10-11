#pragma once
#include "detent/model.hh"
#include "paged_layout.hh"

namespace detent::local::paged {
inline constexpr std::size_t definition_header_bytes = 24, override_header_bytes = 4;
struct Definition {
	std::uint64_t revision = 0;
	bool boolean = false, default_bool = false;
	std::int64_t default_int = 0;
	std::optional<std::uint8_t> rollout;
	std::uint32_t overrides = 0;
};
struct Override {
	std::string_view key;
	bool percentage = false;
	std::uint8_t value = 0;
};
void encode_definition_header(std::span<char>, const Definition &, std::uint32_t maximum_overrides = 256);
void encode_override(std::span<char>, const Override &);
std::size_t definition_size(const KnobDef &, std::uint32_t maximum_overrides = 256);
void encode_definition(std::span<char>, const KnobDef &, std::uint64_t revision,
					   std::uint32_t maximum_overrides = 256);
// Checks fixed framing; callers still validate the full override body.
Definition decode_definition_header(std::span<const char>, std::uint64_t namespace_revision,
									std::uint32_t maximum_overrides = 256);
Definition validate_definition(std::span<const char>, std::uint64_t namespace_revision,
							   std::uint32_t maximum_overrides = 256);
// Advances over one checked length-framed override without allocating.
Override read_override(std::span<const char>, std::size_t &offset);
} // namespace detent::local::paged
