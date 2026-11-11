#pragma once
#include "detent/model.hh"
#include "paged_layout.hh"

namespace detent::local::paged {
// A definition body (docs/design/one-knob-type.md): revision u64, default i64, rollout percentage
// (255 = none), flags (bit 0: rollout value present), override count u16, then the rollout value
// i64 when present. Each override row: key length u8, kind u8 (0 forced, 1 percentage), value
// i64 (the forced value or the percentage), key bytes. Keys are strictly ascending.
inline constexpr std::size_t definition_header_bytes = 20, rollout_value_bytes = 8, override_header_bytes = 10;
struct Definition {
	std::uint64_t revision = 0;
	std::int64_t default_value = 0;
	std::optional<std::uint8_t> rollout;
	std::optional<std::int64_t> rollout_value;
	std::uint32_t overrides = 0;
};
struct Override {
	std::string_view key;
	GroupOverride value;
};
// Encoded header bytes: the fixed part plus the rollout value when present.
constexpr std::size_t header_size(const Definition &definition) noexcept {
	return definition_header_bytes + (definition.rollout_value ? rollout_value_bytes : 0);
}
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
