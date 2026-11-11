#pragma once
#include "detent/model.hh"
#include "paged_layout.hh"

namespace detent::local::paged {
// A definition body (docs/design/one-knob-type.md): revision u64, rollout percentage u8 (255 =
// none), flags u8 (bit 0: rollout value present), default, override count, then the rollout value
// when present. Each override row: key length u8, kind u8 (0 forced, 1 percentage), value (forced:
// varint, percentage: u8), key bytes. Integers after the flags are canonical LEB128, signed ones
// zigzagged. Keys are strictly ascending.
inline constexpr std::size_t definition_header_bytes = 12;   // the smallest header
inline constexpr std::size_t maximum_header_bytes = 33;      // count below 2^14
struct Definition {
	std::uint64_t revision = 0;
	std::int64_t default_value = 0;
	std::optional<std::uint8_t> rollout;
	std::optional<std::int64_t> rollout_value;
	std::uint32_t overrides = 0;
	std::uint64_t key_bytes = 0;   // the override keys' total; set by validate_definition only
};
struct Override {
	std::string_view key;
	GroupOverride value;
};
constexpr std::uint64_t zigzag(std::int64_t value) noexcept {
	return (std::uint64_t(value) << 1) ^ std::uint64_t(value >> 63);
}
constexpr std::size_t varint_size(std::uint64_t value) noexcept {
	std::size_t size = 1;
	for (; value >= 0x80; value >>= 7)
		++size;
	return size;
}
// Encoded header bytes.
constexpr std::size_t header_size(const Definition &definition) noexcept {
	return 10 + varint_size(zigzag(definition.default_value)) + varint_size(definition.overrides) +
		   (definition.rollout_value ? varint_size(zigzag(*definition.rollout_value)) : 0);
}
// Encoded override row bytes, key included.
constexpr std::size_t override_size(const Override &value) noexcept {
	const auto *forced = std::get_if<std::int64_t>(&value.value);
	return 2 + (forced ? varint_size(zigzag(*forced)) : 1) + value.key.size();
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
// Advances over one bounds-checked override row without allocating; validate_definition also
// checks keys are UTF-8 and ascending.
Override read_override(std::span<const char>, std::size_t &offset);
} // namespace detent::local::paged
