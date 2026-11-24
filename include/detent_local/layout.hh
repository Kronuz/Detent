#pragma once
#include "detent/cursor.hh"
#include <cstdint>
#include <span>

namespace detent::local {
[[noreturn]] void refuse_integer_bounds(); // throws std::invalid_argument
// Big-endian integers of 1 to 8 bytes; both throw std::invalid_argument out of bounds or range. The read is
// inline: every lookup reads page headers, node slots and bodies with it.
inline std::uint64_t read_integer(std::span<const char> bytes, std::size_t offset, unsigned width) {
	if (!width || width > 8 || offset > bytes.size() || width > bytes.size() - offset) [[unlikely]]
		refuse_integer_bounds();
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i)
		value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
	return value;
}
void write_integer(std::span<char> bytes, std::size_t offset, std::uint64_t value, unsigned width);
// A generation's identity: a configured cursor identity and the one supported profile; throws otherwise.
void validate_identity(const distribution::NamespaceCursor &, const distribution::Profile &);
} // namespace detent::local
