#pragma once
#include "detent/cursor.hh"
#include <array>
#include <span>
#include <string_view>

namespace detent::local {
inline constexpr std::uint64_t header_bytes = 192, namespace_bytes = 24, record_bytes = 48,
							   override_bytes = 24;
struct Layout {
	std::uint32_t namespaces = 0, records = 0;
	std::uint64_t overrides = 0, strings = 0;
	std::uint64_t namespace_offset = header_bytes, record_offset = header_bytes,
				  override_offset = header_bytes, string_offset = header_bytes, length = header_bytes;
};
struct Header {
	Layout layout;
	distribution::Cursor cursor;
	distribution::Profile profile;
};
Layout calculate_layout(std::uint32_t namespaces, std::uint32_t records, std::uint64_t overrides,
						std::uint64_t strings, std::uint64_t maximum = 256ull * 1024 * 1024);
std::array<char, header_bytes> encode_header(const Header &header);
Header decode_header(std::span<const char> bytes, std::uint64_t maximum = 256ull * 1024 * 1024);
std::uint64_t read_integer(std::span<const char> bytes, std::size_t offset, unsigned width);
void write_integer(std::span<char> bytes, std::size_t offset, std::uint64_t value, unsigned width);
} // namespace detent::local
