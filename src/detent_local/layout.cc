#include "detent_local/layout.hh"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace detent::local {
namespace {
constexpr std::string_view magic = "DTIMAGE1";
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
std::uint64_t add_region(std::uint64_t offset, std::uint64_t count, std::uint64_t width,
						 std::uint64_t maximum) {
	require(offset <= maximum && count <= (maximum - offset) / width,
			"local image region exceeds admitted size");
	return offset + count * width;
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
Layout calculate_layout(std::uint32_t namespaces, std::uint32_t records, std::uint64_t overrides,
						std::uint64_t strings, std::uint64_t maximum) {
	Layout result;
	result.namespaces = namespaces;
	result.records = records;
	result.overrides = overrides;
	result.strings = strings;
	result.record_offset = add_region(header_bytes, namespaces, namespace_bytes, maximum);
	result.override_offset = add_region(result.record_offset, records, record_bytes, maximum);
	result.string_offset = add_region(result.override_offset, overrides, override_bytes, maximum);
	result.length = add_region(result.string_offset, strings, 1, maximum);
	return result;
}
std::array<char, header_bytes> encode_header(const Header &header) {
	const auto &layout = header.layout;
	auto computed = calculate_layout(layout.namespaces, layout.records, layout.overrides, layout.strings,
									 std::numeric_limits<std::uint64_t>::max());
	require(layout.namespace_offset == computed.namespace_offset &&
				layout.record_offset == computed.record_offset &&
				layout.override_offset == computed.override_offset &&
				layout.string_offset == computed.string_offset && layout.length == computed.length,
			"noncanonical local image layout");
	require(header.cursor.cluster != distribution::Identity{} &&
				header.cursor.group != distribution::Identity{} &&
				header.cursor.restore_epoch != distribution::Identity{} &&
				header.cursor.scope != distribution::ScopeIdentity{},
			"unconfigured local image identity");
	require(header.profile.semantic_id == 1 && !(header.profile.required_capabilities & ~1ull),
			"unsupported local image profile");
	std::array<char, header_bytes> result{};
	std::copy(magic.begin(), magic.end(), result.begin());
	write_integer(result, 8, 1, 4);
	write_integer(result, 12, header_bytes, 4);
	write_integer(result, 16, layout.length, 8);
	write_integer(result, 24, layout.namespaces, 4);
	write_integer(result, 28, layout.records, 4);
	write_integer(result, 32, layout.overrides, 8);
	write_integer(result, 40, layout.namespace_offset, 8);
	write_integer(result, 48, layout.record_offset, 8);
	write_integer(result, 56, layout.override_offset, 8);
	write_integer(result, 64, layout.string_offset, 8);
	write_integer(result, 72, layout.strings, 8);
	write_integer(result, 80, header.cursor.revision, 8);
	std::copy(header.cursor.cluster.begin(), header.cursor.cluster.end(), result.begin() + 88);
	std::copy(header.cursor.group.begin(), header.cursor.group.end(), result.begin() + 104);
	std::copy(header.cursor.restore_epoch.begin(), header.cursor.restore_epoch.end(), result.begin() + 120);
	std::copy(header.cursor.scope.begin(), header.cursor.scope.end(), result.begin() + 136);
	write_integer(result, 168, header.profile.semantic_id, 4);
	write_integer(result, 176, header.profile.required_capabilities, 8);
	return result;
}
Header decode_header(std::span<const char> bytes, std::uint64_t maximum) {
	require(bytes.size() >= header_bytes && std::string_view(bytes.data(), 8) == magic &&
				read_integer(bytes, 8, 4) == 1 && read_integer(bytes, 12, 4) == header_bytes,
			"unsupported local image header");
	require(!read_integer(bytes, 172, 4) && !read_integer(bytes, 184, 8),
			"unknown local image header fields");
	Header result;
	result.layout = calculate_layout(static_cast<std::uint32_t>(read_integer(bytes, 24, 4)),
									 static_cast<std::uint32_t>(read_integer(bytes, 28, 4)),
									 read_integer(bytes, 32, 8), read_integer(bytes, 72, 8), maximum);
	require(read_integer(bytes, 16, 8) == result.layout.length &&
				read_integer(bytes, 40, 8) == result.layout.namespace_offset &&
				read_integer(bytes, 48, 8) == result.layout.record_offset &&
				read_integer(bytes, 56, 8) == result.layout.override_offset &&
				read_integer(bytes, 64, 8) == result.layout.string_offset,
			"local image region mismatch");
	result.cursor.revision = read_integer(bytes, 80, 8);
	std::copy_n(bytes.data() + 88, 16, result.cursor.cluster.begin());
	std::copy_n(bytes.data() + 104, 16, result.cursor.group.begin());
	std::copy_n(bytes.data() + 120, 16, result.cursor.restore_epoch.begin());
	std::copy_n(bytes.data() + 136, 32, result.cursor.scope.begin());
	result.profile.semantic_id = static_cast<std::uint32_t>(read_integer(bytes, 168, 4));
	result.profile.required_capabilities = read_integer(bytes, 176, 8);
	(void)encode_header(result);
	return result;
}
} // namespace detent::local
