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
std::size_t put_varint(std::span<char> bytes, std::size_t offset, std::uint64_t value) {
	for (; value >= 0x80; value >>= 7)
		bytes[offset++] = static_cast<char>(value | 0x80);
	bytes[offset++] = static_cast<char>(value);
	return offset;
}
// A canonical LEB128 value: at most ten bytes, no trailing zero group, no bits past 64.
std::uint64_t get_varint(std::span<const char> bytes, std::size_t &offset) {
	std::uint64_t value = 0;
	for (unsigned shift = 0;; shift += 7) {
		require(offset < bytes.size() && shift < 64);
		const auto byte = static_cast<unsigned char>(bytes[offset++]);
		require(shift != 63 || byte <= 1);
		value |= std::uint64_t(byte & 0x7f) << shift;
		if (!(byte & 0x80)) {
			require(byte || !shift);
			return value;
		}
	}
}
std::int64_t unzigzag(std::uint64_t value) {
	return std::bit_cast<std::int64_t>((value >> 1) ^ (0 - (value & 1)));
}
} // namespace
std::size_t definition_size(const KnobDef &definition, std::uint32_t maximum_overrides) {
	require(definition.overrides.size() <= maximum_overrides && !definition_error(definition));
	Definition header;
	header.default_value = definition.default_value;
	header.rollout_value = definition.rollout_value;
	header.overrides = definition.overrides.size();
	std::size_t size = header_size(header);
	std::string_view previous;
	for (const auto &[key, override] : definition.overrides) {
		require(is_valid_group_key(key) && valid_utf8(key, false) && (previous.empty() || less(previous, key)));
		const auto row = override_size({key, override});
		require(size <= page_bytes && row <= page_bytes - size);
		size += row;
		previous = key;
	}
	return size;
}
void encode_definition_header(std::span<char> bytes, const Definition &definition,
							  std::uint32_t maximum_overrides) {
	require(bytes.size() == header_size(definition) && definition.revision &&
			definition.overrides <= maximum_overrides && definition.overrides < (1u << 14) &&
			(!definition.rollout || *definition.rollout <= 100) &&
			(!definition.rollout || definition.rollout_value));
	write_integer(bytes, 0, definition.revision, 8);
	bytes[8] = static_cast<char>(definition.rollout ? *definition.rollout : 255);
	bytes[9] = definition.rollout_value.has_value();
	auto offset = put_varint(bytes, 10, zigzag(definition.default_value));
	offset = put_varint(bytes, offset, definition.overrides);
	if (definition.rollout_value)
		put_varint(bytes, offset, zigzag(*definition.rollout_value));
}
void encode_override(std::span<char> bytes, const Override &value) {
	const auto *rollout = std::get_if<GroupRollout>(&value.value);
	require(is_valid_group_key(value.key) && valid_utf8(value.key, false) && bytes.size() == override_size(value) &&
			(!rollout || rollout->rollout_pct <= 100));
	bytes[0] = static_cast<char>(value.key.size());
	bytes[1] = rollout != nullptr;
	std::size_t offset = 2;
	if (rollout)
		bytes[offset++] = static_cast<char>(rollout->rollout_pct);
	else
		offset = put_varint(bytes, offset, zigzag(std::get<std::int64_t>(value.value)));
	std::copy(value.key.begin(), value.key.end(), bytes.begin() + offset);
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
		const auto size = override_size({key, override});
		encode_override(bytes.subspan(offset, size), {key, override});
		offset += size;
	}
}
Override read_override(std::span<const char> bytes, std::size_t &offset) {
	require(offset <= bytes.size() && bytes.size() - offset >= 3);
	const auto length = static_cast<unsigned char>(bytes[offset]);
	const auto kind = static_cast<unsigned char>(bytes[offset + 1]);
	require(length && length <= 128 && kind <= 1);
	auto at = offset + 2;
	GroupOverride value;
	if (kind) {
		const auto percentage = static_cast<unsigned char>(bytes[at++]);
		require(percentage <= 100);
		value = GroupRollout{percentage};
	} else
		value = unzigzag(get_varint(bytes, at));
	require(length <= bytes.size() - at);
	offset = at + length;
	return {{bytes.data() + at, length}, value};
}
Definition decode_definition_header(std::span<const char> bytes, std::uint64_t namespace_revision,
									std::uint32_t maximum_overrides) {
	require(bytes.size() >= definition_header_bytes && bytes.size() <= page_bytes);
	Definition result;
	result.revision = read_integer(bytes, 0, 8);
	require(result.revision && result.revision <= namespace_revision);
	const auto rollout = static_cast<unsigned char>(bytes[8]);
	const auto flags = static_cast<unsigned char>(bytes[9]);
	require((rollout == 255 || rollout <= 100) && flags <= 1 && (rollout == 255 || flags));
	std::size_t offset = 10;
	result.default_value = unzigzag(get_varint(bytes, offset));
	const auto count = get_varint(bytes, offset);
	require(count <= maximum_overrides && count < (1u << 14));
	result.overrides = static_cast<std::uint32_t>(count);
	if (rollout != 255)
		result.rollout = rollout;
	if (flags)
		result.rollout_value = unzigzag(get_varint(bytes, offset));
	return result;
}
Definition validate_definition(std::span<const char> bytes, std::uint64_t namespace_revision,
							   std::uint32_t maximum_overrides) {
	auto result = decode_definition_header(bytes, namespace_revision, maximum_overrides);
	std::size_t offset = header_size(result);
	std::string_view previous;
	bool percentage = result.rollout.has_value();
	for (std::uint32_t i = 0; i < result.overrides; ++i) {
		const auto item = read_override(bytes, offset);
		require(valid_utf8(item.key, false) && (previous.empty() || less(previous, item.key)));
		percentage = percentage || std::holds_alternative<GroupRollout>(item.value);
		result.key_bytes += item.key.size();
		previous = item.key;
	}
	require(offset == bytes.size() && percentage == result.rollout_value.has_value());
	return result;
}
} // namespace detent::local::paged
