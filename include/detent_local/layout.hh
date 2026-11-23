#pragma once
#include "detent/cursor.hh"
#include <cstdint>
#include <span>

namespace detent::local {
// Big-endian integers of 1 to 8 bytes; both throw std::invalid_argument out of bounds or range.
std::uint64_t read_integer(std::span<const char> bytes, std::size_t offset, unsigned width);
void write_integer(std::span<char> bytes, std::size_t offset, std::uint64_t value, unsigned width);
// A generation's identity: a configured cursor identity and the one supported profile; throws otherwise.
void validate_identity(const distribution::NamespaceCursor &, const distribution::Profile &);
} // namespace detent::local
