#include "detent_local/paged_definition.hh"
#include "detent/utf8.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace detent::local::paged {
namespace {
void require(bool ok) {
	if (!ok)
		throw std::invalid_argument("invalid paged definition");
}
bool less(std::string_view a, std::string_view b) {
	return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
										[](unsigned char x, unsigned char y) { return x < y; });
}
} // namespace
std::size_t definition_size(const KnobDef &definition, std::uint32_t maximum_overrides) {
	require(definition.overrides.size() <= maximum_overrides && !definition_error(definition));
	std::size_t size = definition_header_bytes + (definition.rollout_value ? rollout_value_bytes : 0);
	std::string_view previous;
	for (const auto &[key, override] : definition.overrides) {
		require(is_valid_group_key(key) && valid_utf8(key, false) && (previous.empty() || less(previous, key)));
		require(size <= page_bytes - override_header_bytes - key.size());
		size += override_header_bytes + key.size();
		previous = key;
	}
	return size;
}
void encode_definition_header(std::span<char> bytes, const Definition &definition,
							  std::uint32_t maximum_overrides) {
	require(bytes.size() == header_size(definition) && definition.revision &&
			definition.overrides <= maximum_overrides && definition.overrides <= 0xffff &&
			(!definition.rollout || *definition.rollout <= 100) &&
			(!definition.rollout || definition.rollout_value));
	std::fill(bytes.begin(), bytes.end(), 0);
	write_integer(bytes, 0, definition.revision, 8);
	write_integer(bytes, 8, std::bit_cast<std::uint64_t>(definition.default_value), 8);
	bytes[16] = static_cast<char>(definition.rollout ? *definition.rollout : 255);
	bytes[17] = definition.rollout_value.has_value();
	write_integer(bytes, 18, definition.overrides, 2);
	if (definition.rollout_value)
		write_integer(bytes, 20, std::bit_cast<std::uint64_t>(*definition.rollout_value), 8);
}
void encode_override(std::span<char> bytes, const Override &value) {
	const auto *rollout = std::get_if<GroupRollout>(&value.value);
	require(is_valid_group_key(value.key) && valid_utf8(value.key, false) &&
			bytes.size() == override_header_bytes + value.key.size() && (!rollout || rollout->rollout_pct <= 100));
	bytes[0] = static_cast<char>(value.key.size());
	bytes[1] = rollout != nullptr;
	write_integer(bytes, 2,
				  rollout ? rollout->rollout_pct : std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value.value)), 8);
	std::copy(value.key.begin(), value.key.end(), bytes.begin() + override_header_bytes);
}
void encode_definition(std::span<char> bytes, const KnobDef &definition, std::uint64_t revision,
					   std::uint32_t maximum_overrides) {
	require(bytes.size() == definition_size(definition, maximum_overrides));
	Definition header;
	header.revision = revision;
	header.default_value = definition.default_value;
	header.rollout = definition.rollout_pct;
	header.rollout_value = definition.rollout_value;
	header.overrides = definition.overrides.size();
	std::size_t offset = header_size(header);
	encode_definition_header(bytes.first(offset), header, maximum_overrides);
	for (const auto &[key, override] : definition.overrides) {
		encode_override(bytes.subspan(offset, override_header_bytes + key.size()), {key, override});
		offset += override_header_bytes + key.size();
	}
}
Override read_override(std::span<const char> bytes, std::size_t &offset) {
	require(offset <= bytes.size() && bytes.size() - offset >= override_header_bytes);
	const auto length = static_cast<unsigned char>(bytes[offset]);
	const auto kind = static_cast<unsigned char>(bytes[offset + 1]);
	const auto value = read_integer(bytes, offset + 2, 8);
	require(length && length <= 128 && length <= bytes.size() - offset - override_header_bytes && kind <= 1 &&
			(!kind || value <= 100));
	Override result{{bytes.data() + offset + override_header_bytes, length},
					kind ? GroupOverride(GroupRollout{static_cast<std::uint8_t>(value)})
						 : GroupOverride(std::bit_cast<std::int64_t>(value))};
	require(valid_utf8(result.key, false));
	offset += override_header_bytes + length;
	return result;
}
Definition decode_definition_header(std::span<const char> bytes, std::uint64_t namespace_revision,
									std::uint32_t maximum_overrides) {
	require(bytes.size() >= definition_header_bytes && bytes.size() <= page_bytes);
	Definition result;
	result.revision = read_integer(bytes, 0, 8);
	require(result.revision && result.revision <= namespace_revision);
	result.default_value = std::bit_cast<std::int64_t>(read_integer(bytes, 8, 8));
	const auto rollout = static_cast<unsigned char>(bytes[16]);
	const auto flags = static_cast<unsigned char>(bytes[17]);
	result.overrides = read_integer(bytes, 18, 2);
	require((rollout == 255 || rollout <= 100) && flags <= 1 && result.overrides <= maximum_overrides &&
			(rollout == 255 || flags));
	if (rollout != 255)
		result.rollout = rollout;
	if (flags) {
		require(bytes.size() >= definition_header_bytes + rollout_value_bytes);
		result.rollout_value = std::bit_cast<std::int64_t>(read_integer(bytes, 20, 8));
	}
	return result;
}
Definition validate_definition(std::span<const char> bytes, std::uint64_t namespace_revision,
							   std::uint32_t maximum_overrides) {
	const auto result = decode_definition_header(bytes, namespace_revision, maximum_overrides);
	std::size_t offset = header_size(result);
	std::string_view previous;
	bool percentage = result.rollout.has_value();
	for (std::uint32_t i = 0; i < result.overrides; ++i) {
		const auto item = read_override(bytes, offset);
		require(previous.empty() || less(previous, item.key));
		percentage = percentage || std::holds_alternative<GroupRollout>(item.value);
		previous = item.key;
	}
	require(offset == bytes.size() && percentage == result.rollout_value.has_value());
	return result;
}
} // namespace detent::local::paged
