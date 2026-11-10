#include "paged_root_prefix.hh"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace detent::local::detail {
namespace p = paged;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("inconsistent paged root prefix");
}
void matches(std::span<const char> bytes, std::size_t offset, std::span<const char> expected) {
	if (bytes.size() <= offset)
		return;
	const auto available = bytes.subspan(offset, std::min(expected.size(), bytes.size() - offset));
	require(std::equal(available.begin(), available.end(), expected.begin()));
}
void zeros(std::span<const char> bytes, std::size_t begin, std::size_t end) {
	if (bytes.size() <= begin)
		return;
	require(std::all_of(bytes.begin() + begin, bytes.begin() + std::min(end, bytes.size()),
						[](char v) { return v == 0; }));
}
void nonzero(std::span<const char> bytes, std::size_t begin, std::size_t length) {
	if (bytes.size() < begin + length)
		return;
	require(
		std::any_of(bytes.begin() + begin, bytes.begin() + begin + length, [](char v) { return v != 0; }));
}
std::uint64_t minimum(std::span<const char> bytes, std::size_t offset, unsigned width) {
	if (bytes.size() <= offset)
		return 0;
	const auto available = std::min<std::size_t>(width, bytes.size() - offset);
	std::uint64_t value = 0;
	for (std::size_t i = 0; i < available; ++i)
		value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
	return value << ((width - available) * 8);
}
} // namespace
std::optional<std::pair<distribution::Cursor, distribution::Profile>>
validate_obsolete_root_prefix(std::span<const char> bytes) {
	require(bytes.size() <= p::root_bytes);
	constexpr std::array<char, 16> framing{'D', 'T', 'R', 'O', 'O', 'T', '0', '2', 0, 0, 0, 2, 0, 0, 1, 64};
	matches(bytes, 0, framing);
	zeros(bytes, 48, 56);
	zeros(bytes, 148, 152);
	zeros(bytes, 280, 320);
	if (bytes.size() < 160)
		return {};
	Header context;
	context.cursor.revision = read_integer(bytes, 56, 8);
	std::copy_n(bytes.begin() + 64, 16, context.cursor.cluster.begin());
	std::copy_n(bytes.begin() + 80, 16, context.cursor.group.begin());
	std::copy_n(bytes.begin() + 96, 16, context.cursor.restore_epoch.begin());
	std::copy_n(bytes.begin() + 112, 32, context.cursor.scope.begin());
	context.profile = {static_cast<std::uint32_t>(read_integer(bytes, 144, 4)), read_integer(bytes, 152, 8)};
	encode_header(context);
	return std::pair(context.cursor, context.profile);
}
std::optional<std::pair<distribution::Cursor, distribution::Profile>>
validate_paged_root_prefix(std::span<const char> bytes) {
	require(bytes.size() <= p::root_bytes);
	constexpr std::array<char, 16> framing{'D', 'T', 'R', 'O', 'O', 'T', '0', '3', 0, 0, 0, 3, 0, 0, 1, 64};
	matches(bytes, 0, framing);
	zeros(bytes, 48, 56);
	zeros(bytes, 148, 152);
	zeros(bytes, 228, 320);
	constexpr std::array<char, 4> semantic{0, 0, 0, 1};
	matches(bytes, 144, semantic);
	zeros(bytes, 152, 159);
	if (bytes.size() >= 160)
		require(!(static_cast<unsigned char>(bytes[159]) & ~1u));
	for (const auto offset : {64u, 80u, 96u})
		nonzero(bytes, offset, 16);
	nonzero(bytes, 112, 32);
	if (bytes.size() >= 40) {
		const auto live = read_integer(bytes, 16, 8), pages = read_integer(bytes, 32, 8);
		const auto keys = read_integer(bytes, 24, 4) + read_integer(bytes, 28, 4);
		if (!pages) {
			require(!live && !keys);
			zeros(bytes, 40, 48);
			zeros(bytes, 160, 228);
		} else {
			// The tree reference is a child entry: content [160,208), placement [208,228).
			require(keys && pages < 2 * keys && pages <= live / p::minimum_page_bytes);
			if (bytes.size() > 40)
				require(minimum(bytes, 40, 8) <= pages);
			if (bytes.size() >= 48)
				require(read_integer(bytes, 40, 8) != 0);
			nonzero(bytes, 160, 32);
			if (bytes.size() >= 200)
				require(read_integer(bytes, 192, 8) == keys);
			if (bytes.size() > 200)
				require(minimum(bytes, 200, 4) <= p::page_bytes);
			if (bytes.size() >= 204) {
				const auto length = read_integer(bytes, 200, 4);
				require(length >= p::minimum_page_bytes && length <= live &&
						pages - 1 <= (live - length) / p::minimum_page_bytes);
			}
			if (bytes.size() >= 205)
				require(bytes[204] != 0);
			zeros(bytes, 205, 208);
			nonzero(bytes, 208, 16);
			if (bytes.size() > 224)
				require(minimum(bytes, 224, 4) <= p::segment_bytes);
			if (bytes.size() >= 228) {
				const auto offset = read_integer(bytes, 224, 4), length = read_integer(bytes, 200, 4);
				require(offset % 8 == 0 && length <= p::segment_bytes - offset);
			}
		}
	}
	if (bytes.size() >= 160) {
		distribution::Cursor cursor;
		cursor.revision = read_integer(bytes, 56, 8);
		std::copy_n(bytes.begin() + 64, 16, cursor.cluster.begin());
		std::copy_n(bytes.begin() + 80, 16, cursor.group.begin());
		std::copy_n(bytes.begin() + 96, 16, cursor.restore_epoch.begin());
		std::copy_n(bytes.begin() + 112, 32, cursor.scope.begin());
		distribution::Profile profile{static_cast<std::uint32_t>(read_integer(bytes, 144, 4)),
									  read_integer(bytes, 152, 8)};
		return std::pair(cursor, profile);
	}
	return {};
}
} // namespace detent::local::detail
