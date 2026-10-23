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
validate_paged_root_prefix(std::span<const char> bytes) {
	require(bytes.size() <= p::root_bytes);
	constexpr std::array<char, 16> framing{'D', 'T', 'R', 'O', 'O', 'T', '0', '2', 0, 0, 0, 2, 0, 0, 1, 64};
	matches(bytes, 0, framing);
	zeros(bytes, 48, 56);
	zeros(bytes, 148, 152);
	zeros(bytes, 280, 320);
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
			zeros(bytes, 160, 248);
			matches(bytes, 248, p::empty_commitment());
		} else {
			require(keys && pages >= keys && pages <= live / p::page_header_bytes);
			if (bytes.size() > 40)
				require(minimum(bytes, 40, 8) <= pages);
			if (bytes.size() >= 48)
				require(read_integer(bytes, 40, 8) != 0);
			nonzero(bytes, 160, 16);
			if (bytes.size() > 176)
				require(minimum(bytes, 176, 4) <= p::segment_bytes);
			if (bytes.size() > 180)
				require(minimum(bytes, 180, 4) <= p::page_bytes);
			if (bytes.size() >= 180)
				require(read_integer(bytes, 176, 4) % 8 == 0);
			if (bytes.size() >= 184) {
				const auto offset = read_integer(bytes, 176, 4), length = read_integer(bytes, 180, 4);
				require(length >= p::page_header_bytes && length <= p::segment_bytes - offset &&
						length <= live);
				require(live - length >= (pages - 1) * p::page_header_bytes);
			}
			nonzero(bytes, 184, 32);
			nonzero(bytes, 216, 32);
			if (bytes.size() >= 248)
				matches(bytes, 248, bytes.subspan(216, 32));
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
