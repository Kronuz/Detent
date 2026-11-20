#include "detent_local/layout.hh"
#include <stdexcept>

namespace detent::local {
namespace {
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
} // namespace
std::uint64_t read_integer(std::span<const char> bytes, std::size_t offset, unsigned width) {
	require(width && width <= 8 && offset <= bytes.size() && width <= bytes.size() - offset,
			"local image integer bounds");
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i)
		value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
	return value;
}
void write_integer(std::span<char> bytes, std::size_t offset, std::uint64_t value, unsigned width) {
	require(width && width <= 8 && offset <= bytes.size() && width <= bytes.size() - offset,
			"local image integer bounds");
	require(width == 8 || value < (1ull << (width * 8)), "local image integer overflow");
	for (unsigned i = width; i; --i)
		bytes[offset + width - i] = static_cast<char>(value >> ((i - 1) * 8));
}
void validate_identity(const distribution::NamespaceCursor &cursor, const distribution::Profile &profile) {
	require(cursor.cluster != distribution::Identity{} && cursor.incarnation != distribution::Identity{},
			"unconfigured local image identity");
	require(profile.semantic_id == 1 && !(profile.required_capabilities & ~1ull), "unsupported local image profile");
}
} // namespace detent::local
