#pragma once
#include "layout.hh"
#include <array>

namespace detent::local::paged {
using Digest = std::array<char, 32>;
inline constexpr std::uint32_t page_bytes = 64 * 1024, segment_bytes = 1024 * 1024;
inline constexpr std::size_t reference_bytes = 88, child_bytes = 96, page_header_bytes = 80, root_bytes = 320;
enum class PageKind : std::uint32_t { Branch = 1, Definition = 2, Namespace = 3, Overflow = 4 };
struct Limits {
	std::uint64_t bytes = 256ull * 1024 * 1024, pages = 1100000, segments = 4096;
	std::uint32_t records = 500000, namespaces = 4096, prefix_bytes = 512;
};
struct Reference {
	distribution::Identity segment{};
	std::uint32_t offset = 0, length = 0;
	Digest physical{}, logical{};
	bool operator==(const Reference &) const = default;
};
struct Child {
	std::uint8_t label = 0;
	Reference reference;
};
struct PageHeader {
	PageKind kind = PageKind::Definition;
	std::uint32_t length = page_header_bytes, prefix = 0, children = 0, body = 0;
	std::uint64_t keys = 0;
	Digest logical{};
};
struct Root {
	distribution::Cursor cursor;
	distribution::Profile profile;
	std::uint64_t bytes = 0, pages = 0, segments = 0;
	std::uint32_t records = 0, namespaces = 0;
	Reference tree;
	Digest logical{};
};
Digest empty_commitment();
std::size_t encode_namespace_key(std::span<char>, std::string_view name);
std::size_t encode_definition_key(std::span<char>, std::string_view qualified_name);
Digest physical_commitment(std::span<const char>);
// Commits semantics and ordered child hashes, excluding segment placement.
Digest logical_commitment(PageKind, std::string_view prefix, std::span<const Child>,
						  std::span<const char> body);
void verify_page(std::span<const char>, const Reference &, Limits = {});
std::array<char, reference_bytes> encode_reference(const Reference &);
Reference decode_reference(std::span<const char>);
std::array<char, child_bytes> encode_child(const Child &);
Child decode_child(std::span<const char>);
std::array<char, page_header_bytes> encode_page_header(const PageHeader &, Limits = {});
// Checks framing only; graph traversal verifies ordering, semantics and hashes.
PageHeader decode_page_header(std::span<const char>, Limits = {});
std::array<char, root_bytes> encode_root(const Root &, Limits = {});
Root decode_root(std::span<const char>, Limits = {});
} // namespace detent::local::paged
