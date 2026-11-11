#include "borrowed_definition.hh"
#include <bit>
#include <stdexcept>

namespace detent::local::detail {
namespace {
void require(bool ok) {
	if (!ok)
		throw std::runtime_error("invalid validated definition region");
}
} // namespace
BorrowedDefinition BorrowedDefinition::from_paged(std::span<const char> body) {
	require(body.size() >= paged::definition_header_bytes);
	return {body, {}, nullptr};
}
std::int64_t BorrowedDefinition::default_value() const {
	return std::bit_cast<std::int64_t>(read_integer(record, flat_layout ? 24 : 8, 8));
}
std::optional<std::uint8_t> BorrowedDefinition::rollout() const {
	if (flat_layout) {
		if (!(read_integer(record, 44, 1) & 1))
			return {};
		return static_cast<std::uint8_t>(read_integer(record, 45, 1));
	}
	const auto value = read_integer(record, 16, 1);
	return value == 255 ? std::nullopt : std::optional(static_cast<std::uint8_t>(value));
}
std::int64_t BorrowedDefinition::rollout_value() const {
	if (flat_layout)
		return std::bit_cast<std::int64_t>(read_integer(record, (read_integer(record, 44, 1) & 2) ? 48 : 24, 8));
	return std::bit_cast<std::int64_t>(read_integer(record, read_integer(record, 17, 1) ? 20 : 8, 8));
}
std::string_view BorrowedDefinition::qualified_key() const {
	if (!flat_layout)
		return {};
	const auto offset = read_integer(record, 0, 8), size = read_integer(record, 8, 4);
	require(offset <= flat_layout->strings && size <= flat_layout->strings - offset);
	const auto strings = flat_image.subspan(flat_layout->string_offset, flat_layout->strings);
	return {strings.data() + offset, static_cast<std::size_t>(size)};
}
std::optional<GroupOverride> BorrowedDefinition::override_for(std::string_view key) const {
	if (!flat_layout) {
		const auto header = paged::definition_header_bytes + (read_integer(record, 17, 1) ? paged::rollout_value_bytes : 0);
		require(record.size() >= header);
		const auto overrides = record.subspan(header);
		const auto count = read_integer(record, 18, 2);
		std::size_t offset = 0;
		for (std::uint32_t i = 0; i < count; ++i) {
			require(offset <= overrides.size() && overrides.size() - offset >= paged::override_header_bytes);
			const auto length = static_cast<unsigned char>(overrides[offset]);
			require(length <= overrides.size() - offset - paged::override_header_bytes);
			const std::string_view name{overrides.data() + offset + paged::override_header_bytes, length};
			if (name == key) {
				const auto kind = static_cast<unsigned char>(overrides[offset + 1]);
				const auto value = read_integer(overrides, offset + 2, 8);
				require(kind <= 1 && (!kind || value <= 100));
				return kind ? GroupOverride(GroupRollout{static_cast<std::uint8_t>(value)})
							: GroupOverride(std::bit_cast<std::int64_t>(value));
			}
			if (name > key)
				return {};
			offset += paged::override_header_bytes + length;
		}
		return {};
	}
	const auto start = read_integer(record, 32, 8), count = read_integer(record, 40, 4);
	require(start <= flat_layout->overrides && count <= flat_layout->overrides - start);
	const auto overrides =
		flat_image.subspan(flat_layout->override_offset + start * override_bytes, count * override_bytes);
	const auto strings = flat_image.subspan(flat_layout->string_offset, flat_layout->strings);
	auto name = [&](std::uint32_t ordinal) {
		const auto row = std::uint64_t(ordinal) * override_bytes;
		const auto start = read_integer(overrides, row, 8), size = read_integer(overrides, row + 8, 4);
		require(start <= strings.size() && size <= strings.size() - start);
		return std::string_view(strings.data() + start, size);
	};
	std::uint32_t low = 0, high = static_cast<std::uint32_t>(count);
	while (low < high) {
		const auto middle = low + (high - low) / 2;
		if (name(middle) < key)
			low = middle + 1;
		else
			high = middle;
	}
	if (low == count || name(low) != key)
		return {};
	const auto row = std::uint64_t(low) * override_bytes;
	const auto kind = read_integer(overrides, row + 12, 1), value = read_integer(overrides, row + 16, 8);
	require(kind <= 1 && (!kind || value <= 100));
	return kind ? GroupOverride(GroupRollout{static_cast<std::uint8_t>(value)})
				: GroupOverride(std::bit_cast<std::int64_t>(value));
}
} // namespace detent::local::detail
