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
	std::size_t size = definition_header_bytes;
	if (const auto *value = std::get_if<BoolDef>(&definition)) {
		require(value->overrides.size() <= maximum_overrides &&
				(!value->rollout_pct || *value->rollout_pct <= 100));
		std::string_view previous;
		for (const auto &[key, override] : value->overrides) {
			require(is_valid_group_key(key) && valid_utf8(key, false) &&
					(previous.empty() || less(previous, key)));
			if (const auto *rollout = std::get_if<GroupRollout>(&override))
				require(rollout->rollout_pct <= 100);
			require(size <= page_bytes - 4 - key.size());
			size += 4 + key.size();
			previous = key;
		}
	}
	return size;
}
void encode_definition_header(std::span<char> bytes, const Definition &definition,
							  std::uint32_t maximum_overrides) {
	require(bytes.size() == definition_header_bytes && definition.revision &&
			definition.overrides <= maximum_overrides &&
			(!definition.rollout || *definition.rollout <= 100) &&
			(definition.boolean || (!definition.overrides && !definition.rollout)));
	std::fill(bytes.begin(), bytes.end(), 0);
	write_integer(bytes, 0, definition.revision, 8);
	if (!definition.boolean) {
		bytes[8] = 2;
		write_integer(bytes, 16, std::bit_cast<std::uint64_t>(definition.default_int), 8);
		return;
	}
	bytes[8] = 1;
	bytes[9] = definition.default_bool;
	bytes[10] = static_cast<char>(definition.rollout ? *definition.rollout : 255);
	write_integer(bytes, 12, definition.overrides, 4);
}
void encode_override(std::span<char> bytes, const Override &value) {
	require(is_valid_group_key(value.key) && valid_utf8(value.key, false) &&
			bytes.size() == 4 + value.key.size() && value.value <= (value.percentage ? 100 : 1));
	write_integer(bytes, 0, value.key.size(), 2);
	bytes[2] = value.percentage;
	bytes[3] = static_cast<char>(value.value);
	std::copy(value.key.begin(), value.key.end(), bytes.begin() + 4);
}
void encode_definition(std::span<char> bytes, const KnobDef &definition, std::uint64_t revision,
					   std::uint32_t maximum_overrides) {
	require(bytes.size() == definition_size(definition, maximum_overrides));
	Definition header;
	header.revision = revision;
	if (const auto *value = std::get_if<IntDef>(&definition)) {
		header.default_int = value->default_value;
		encode_definition_header(bytes, header, maximum_overrides);
		return;
	}
	const auto &value = std::get<BoolDef>(definition);
	header.boolean = true;
	header.default_bool = value.default_value;
	header.rollout = value.rollout_pct;
	header.overrides = value.overrides.size();
	encode_definition_header(bytes.first(definition_header_bytes), header, maximum_overrides);
	std::size_t offset = definition_header_bytes;
	for (const auto &[key, override] : value.overrides) {
		const auto *rollout = std::get_if<GroupRollout>(&override);
		encode_override(
			bytes.subspan(offset, 4 + key.size()),
			{key, rollout != nullptr,
			 rollout ? rollout->rollout_pct : static_cast<std::uint8_t>(std::get<bool>(override))});
		offset += 4 + key.size();
	}
}
Override read_override(std::span<const char> bytes, std::size_t &offset) {
	require(offset <= bytes.size() && bytes.size() - offset >= 4);
	const auto length = read_integer(bytes, offset, 2);
	const auto kind = static_cast<unsigned char>(bytes[offset + 2]);
	const auto value = static_cast<unsigned char>(bytes[offset + 3]);
	require(length && length <= 128 && length <= bytes.size() - offset - 4 && kind <= 1 &&
			value <= (kind ? 100 : 1));
	Override result{{bytes.data() + offset + 4, static_cast<std::size_t>(length)}, kind == 1, value};
	require(valid_utf8(result.key, false));
	offset += 4 + length;
	return result;
}
Definition validate_definition(std::span<const char> bytes, std::uint64_t namespace_revision,
							   std::uint32_t maximum_overrides) {
	require(bytes.size() >= definition_header_bytes && bytes.size() <= page_bytes);
	Definition result;
	result.revision = read_integer(bytes, 0, 8);
	require(result.revision && result.revision <= namespace_revision && !bytes[11]);
	const auto kind = static_cast<unsigned char>(bytes[8]);
	result.overrides = read_integer(bytes, 12, 4);
	if (kind == 2) {
		require(bytes.size() == definition_header_bytes && !bytes[9] && !bytes[10] && !result.overrides);
		result.default_int = std::bit_cast<std::int64_t>(read_integer(bytes, 16, 8));
		return result;
	}
	require(kind == 1 && static_cast<unsigned char>(bytes[9]) <= 1 && !read_integer(bytes, 16, 8) &&
			result.overrides <= maximum_overrides);
	result.boolean = true;
	result.default_bool = bytes[9];
	const auto rollout = static_cast<unsigned char>(bytes[10]);
	require(rollout == 255 || rollout <= 100);
	if (rollout != 255)
		result.rollout = rollout;
	std::size_t offset = definition_header_bytes;
	std::string_view previous;
	for (std::uint32_t i = 0; i < result.overrides; ++i) {
		const auto item = read_override(bytes, offset);
		require(previous.empty() || less(previous, item.key));
		previous = item.key;
	}
	require(offset == bytes.size());
	return result;
}
} // namespace detent::local::paged
