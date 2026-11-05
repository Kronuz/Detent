#pragma once
#include "layout.hh"
#include <array>
#include <stdexcept>

namespace detent::local::paged {
using Digest = std::array<char, 32>;
class UnsupportedFormat final : public std::invalid_argument {
  public:
    UnsupportedFormat() : std::invalid_argument("unsupported paged local format") {}
};
inline constexpr std::uint32_t page_bytes = 64 * 1024, segment_bytes = 1024 * 1024;
inline constexpr std::size_t reference_bytes = 65, content_bytes = 48, placement_bytes = 20,
							 child_bytes = content_bytes + placement_bytes, page_header_bytes = 64,
							 minimum_page_bytes = 79, root_bytes = 320, page_capacity = 4096,
							 maximum_leaf_body_bytes = page_bytes - 80;
enum class PageKind : std::uint8_t { Branch = 1, Definition = 2, Namespace = 3 };
struct Limits {
	std::uint64_t bytes = 256ull * 1024 * 1024, pages = 1100000, segments = 4096;
	std::uint32_t records = 500000, namespaces = 4096, prefix_bytes = 512;
};
struct Reference {
	distribution::Identity segment{};
	std::uint32_t offset = 0, length = 0;
	std::uint64_t keys = 0;
	std::uint8_t height = 0;
	Digest digest{};
	bool operator==(const Reference &) const = default;
};
struct Child {
	std::uint8_t label = 0;
	Reference reference;
};
struct PageHeader {
	std::uint32_t length = minimum_page_bytes;
	std::uint16_t externals = 0, nodes = 1;
	std::uint64_t keys = 1;
	std::uint8_t height = 1;
	Digest digest{};
};
struct Root {
	distribution::Cursor cursor;
	distribution::Profile profile;
	std::uint64_t bytes = 0, pages = 0, segments = 0;
	std::uint32_t records = 0, namespaces = 0;
	Reference tree;
};
Digest empty_commitment();
Digest state_commitment(const Root &);
std::size_t encode_namespace_key(std::span<char>, std::string_view name);
std::size_t encode_definition_key(std::span<char>, std::string_view qualified_name);
struct Key {
	PageKind kind;
	std::string_view name_space, local_name;
};
Key decode_key(std::string_view);
// Hashes [0,32) and [64,L-20E); framing must be valid before deriving these
// ranges.
Digest page_commitment(std::span<const char>, Limits = {});
std::uint64_t page_digests() noexcept;
enum class HashKind { Page, Pack, Metadata };
struct HashCounts {
	std::uint64_t page_calls = 0, page_bytes = 0, pack_calls = 0, pack_bytes = 0,
				  metadata_calls = 0, metadata_bytes = 0;
	bool operator==(const HashCounts &) const = default;
};
HashCounts hash_counts() noexcept;
void reset_hash_counts() noexcept;
void count_hash(HashKind, std::uint64_t calls, std::uint64_t bytes) noexcept;
Digest pack_commitment(std::span<const char>);
Digest metadata_commitment(std::span<const char>);
std::array<char, reference_bytes> encode_reference(const Reference &);
Reference decode_reference(std::span<const char>);
std::array<char, content_bytes> encode_content(const Reference &);
std::array<char, placement_bytes> encode_placement(const Reference &);
Reference decode_external(std::span<const char>, std::uint16_t index, Limits = {});
std::array<char, page_header_bytes> encode_page_header(const PageHeader &, Limits = {});
PageHeader decode_page_header(std::span<const char>, Limits = {});
std::array<char, root_bytes> encode_root(const Root &, Limits = {});
Root decode_root(std::span<const char>, Limits = {});
struct NodeView {
	PageKind kind;
	std::string_view prefix;
	std::span<const char> body, labels, slots;
	std::size_t end = 0;
	std::uint16_t slot(std::size_t index) const;
};
// Bounds each visited node against the current header and table boundaries.
NodeView decode_node(std::span<const char>, std::size_t offset, Limits = {});
// Verifies digest, reference summaries and exact preorder structure without
// allocation.
void verify_page_structure(std::span<const char>, const Reference &, Limits = {});
void verify_page(std::span<const char>, const Reference &, Limits = {});
} // namespace detent::local::paged
