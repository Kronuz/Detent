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
void encode_definition(std::span<char> bytes, const KnobDef &definition, std::uint64_t revision,
					   std::uint32_t maximum_overrides) {
	require(revision && bytes.size() == definition_size(definition, maximum_overrides));
	std::fill(bytes.begin(), bytes.end(), 0);
	write_integer(bytes, 0, revision, 8);
	if (const auto *value = std::get_if<IntDef>(&definition)) {
		bytes[8] = 2;
		write_integer(bytes, 16, std::bit_cast<std::uint64_t>(value->default_value), 8);
		return;
	}
	const auto &value = std::get<BoolDef>(definition);
	bytes[8] = 1;
	bytes[9] = value.default_value;
	bytes[10] = static_cast<char>(value.rollout_pct ? *value.rollout_pct : 255);
	write_integer(bytes, 12, value.overrides.size(), 4);
	std::size_t offset = definition_header_bytes;
	for (const auto &[key, override] : value.overrides) {
		write_integer(bytes, offset, key.size(), 2);
		if (const auto *rollout = std::get_if<GroupRollout>(&override)) {
			bytes[offset + 2] = 1;
			bytes[offset + 3] = rollout->rollout_pct;
		} else
			bytes[offset + 3] = std::get<bool>(override);
		std::copy(key.begin(), key.end(), bytes.begin() + offset + 4);
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
