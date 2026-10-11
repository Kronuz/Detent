#include "detent_local/paged_layout.hh"
#include "detent/validation.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::paged {
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid paged local image");
}
template <std::size_t N> bool nonzero(const std::array<char, N> &value) {
	return std::any_of(value.begin(), value.end(), [](char byte) { return byte != 0; });
}
template <std::size_t N>
void copy(std::span<const char> bytes, std::size_t offset, std::array<char, N> &out) {
	require(offset <= bytes.size() && N <= bytes.size() - offset);
	std::copy_n(bytes.begin() + offset, N, out.begin());
}
template <std::size_t N>
void put(std::span<char> bytes, std::size_t offset, const std::array<char, N> &value) {
	std::copy(value.begin(), value.end(), bytes.begin() + offset);
}
void validate(const Reference &reference) {
	require(nonzero(reference.segment) && nonzero(reference.physical) && nonzero(reference.logical) &&
			reference.offset % 8 == 0 && reference.offset <= segment_bytes &&
			reference.length >= page_header_bytes && reference.length <= page_bytes &&
			reference.length <= segment_bytes - reference.offset);
}
void validate(const PageHeader &header, const Limits &limits) {
	require(header.prefix <= limits.prefix_bytes && header.children <= 256 &&
			header.length >= page_header_bytes && header.length <= page_bytes &&
			header.length <= limits.bytes &&
			header.keys <= std::uint64_t(limits.records) + limits.namespaces && nonzero(header.logical));
	const auto length =
		page_header_bytes + std::uint64_t(header.prefix) + child_bytes * header.children + header.body;
	require(length == header.length);
	switch (header.kind) {
	case PageKind::Branch:
		require(header.children >= 2 && !header.body && header.keys >= header.children);
		break;
	case PageKind::Definition:
		require(!header.children && header.body && header.keys == 1);
		break;
	case PageKind::Namespace:
		require(!header.children && header.body == 8 && header.keys == 1);
		break;
	case PageKind::Overflow:
		require(!header.children && !header.prefix && header.body && !header.keys);
		break;
	default:
		require(false);
	}
}
void validate(const Root &root, const Limits &limits) {
	Header context;
	context.cursor = root.cursor;
	context.profile = root.profile;
	encode_header(context);
	require(root.records <= limits.records && root.namespaces <= limits.namespaces &&
			root.pages <= limits.pages && root.segments <= limits.segments && root.bytes <= limits.bytes);
	if (!root.pages) {
		require(!root.records && !root.namespaces && !root.bytes && !root.segments &&
				root.tree == Reference{} && root.logical == empty_commitment());
	} else {
		validate(root.tree);
		require(root.segments && root.segments <= root.pages &&
				root.pages <= root.bytes / page_header_bytes && root.tree.length <= root.bytes &&
				root.bytes - root.tree.length >= (root.pages - 1) * page_header_bytes &&
				root.logical == root.tree.logical && std::uint64_t(root.records) + root.namespaces > 0 &&
				root.pages >= std::uint64_t(root.records) + root.namespaces);
	}
}
} // namespace
std::size_t encode_namespace_key(std::span<char> out, std::string_view name) {
	require(!name.empty() && name.size() <= 198 &&
			std::all_of(name.begin(), name.end(), detent::detail::is_name_char));
	const auto size = name.size() + 2;
	require(out.size() >= size);
	out[0] = 'W';
	std::copy(name.begin(), name.end(), out.begin() + 1);
	out[size - 1] = 0;
	return size;
}
std::size_t encode_definition_key(std::span<char> out, std::string_view name) {
	require(is_valid_knob_name(name));
	const auto size = name.size() + 2;
	require(out.size() >= size);
	out[0] = 'K';
	std::copy(name.begin(), name.end(), out.begin() + 1);
	out[1 + name.find(':')] = 0;
	out[size - 1] = 0;
	return size;
}
Key decode_key(std::string_view bytes) {
	require(bytes.size() >= 3 && bytes.size() <= 202 && bytes.back() == 0);
	const auto separator = bytes.find('\0', 1);
	require(separator != std::string_view::npos && separator > 1);
	const auto name_space = bytes.substr(1, separator - 1);
	require(name_space.size() <= 198 &&
			std::all_of(name_space.begin(), name_space.end(), detent::detail::is_name_char));
	if (bytes.front() == 'W') {
		require(separator == bytes.size() - 1);
		return {PageKind::Namespace, name_space, {}};
	}
	require(bytes.front() == 'K' && separator + 2 < bytes.size());
	const auto local = bytes.substr(separator + 1, bytes.size() - separator - 2);
	require(name_space.size() + local.size() + 1 <= 200 &&
			std::all_of(local.begin(), local.end(), detent::detail::is_name_char));
	return {PageKind::Definition, name_space, local};
}
Digest physical_commitment(std::span<const char> bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<unsigned char, 32> value{};
	hash.getHash(value.data());
	Digest result;
	std::copy(value.begin(), value.end(), result.begin());
	return result;
}
Digest logical_commitment(PageKind kind, std::string_view prefix, std::span<const Child> children,
						  std::span<const char> body) {
	require(prefix.size() <= 512 && children.size() <= 256 && body.size() <= page_bytes);
	PageHeader framing{kind,
					   static_cast<std::uint32_t>(page_header_bytes + prefix.size() +
												  child_bytes * children.size() + body.size()),
					   static_cast<std::uint32_t>(prefix.size()),
					   static_cast<std::uint32_t>(children.size()),
					   static_cast<std::uint32_t>(body.size()),
					   kind == PageKind::Branch		? children.size()
					   : kind == PageKind::Overflow ? 0u
													: 1u,
					   {}};
	framing.logical.fill(1);
	validate(framing, {});
	SHA256 hash;
	constexpr std::string_view domain = "detent.local.logical.v2.node";
	hash.add(domain.data(), domain.size());
	std::array<char, 16> lengths{};
	write_integer(lengths, 0, static_cast<std::uint32_t>(kind), 4);
	write_integer(lengths, 4, prefix.size(), 4);
	write_integer(lengths, 8, children.size(), 4);
	write_integer(lengths, 12, body.size(), 4);
	hash.add(lengths.data(), lengths.size());
	hash.add(prefix.data(), prefix.size());
	int previous = -1;
	for (const auto &child : children) {
		require(child.label > previous && nonzero(child.reference.logical));
		previous = child.label;
		hash.add(&child.label, 1);
		hash.add(child.reference.logical.data(), child.reference.logical.size());
	}
	hash.add(body.data(), body.size());
	std::array<unsigned char, 32> value{};
	hash.getHash(value.data());
	Digest result;
	std::copy(value.begin(), value.end(), result.begin());
	return result;
}
void verify_page(std::span<const char> bytes, const Reference &reference, Limits limits) {
	validate(reference);
	require(bytes.size() == reference.length && physical_commitment(bytes) == reference.physical);
	const auto header = decode_page_header(bytes, limits);
	require(header.logical == reference.logical);
	const auto prefix = bytes.subspan(page_header_bytes, header.prefix);
	std::array<Child, 256> children{};
	for (std::uint32_t i = 0; i < header.children; ++i)
		children[i] =
			decode_child(bytes.subspan(page_header_bytes + header.prefix + i * child_bytes, child_bytes));
	const auto body = bytes.last(header.body);
	require(logical_commitment(header.kind, {prefix.data(), prefix.size()},
							   std::span<const Child>(children).first(header.children),
							   body) == header.logical);
}
Digest empty_commitment() {
	SHA256 hash;
	constexpr std::string_view domain = "detent.local.logical.v2.empty";
	hash.add(domain.data(), domain.size());
	std::array<unsigned char, 32> bytes{};
	hash.getHash(bytes.data());
	Digest result;
	std::copy(bytes.begin(), bytes.end(), result.begin());
	return result;
}
std::array<char, reference_bytes> encode_reference(const Reference &reference) {
	validate(reference);
	std::array<char, reference_bytes> bytes{};
	put(bytes, 0, reference.segment);
	write_integer(bytes, 16, reference.offset, 4);
	write_integer(bytes, 20, reference.length, 4);
	put(bytes, 24, reference.physical);
	put(bytes, 56, reference.logical);
	return bytes;
}
Reference decode_reference(std::span<const char> bytes) {
	require(bytes.size() == reference_bytes);
	Reference reference;
	copy(bytes, 0, reference.segment);
	reference.offset = read_integer(bytes, 16, 4);
	reference.length = read_integer(bytes, 20, 4);
	copy(bytes, 24, reference.physical);
	copy(bytes, 56, reference.logical);
	validate(reference);
	return reference;
}
std::array<char, child_bytes> encode_child(const Child &child) {
	std::array<char, child_bytes> bytes{};
	bytes[0] = static_cast<char>(child.label);
	const auto reference = encode_reference(child.reference);
	put(bytes, 8, reference);
	return bytes;
}
Child decode_child(std::span<const char> bytes) {
	require(bytes.size() == child_bytes && !read_integer(bytes, 1, 7));
	return {static_cast<std::uint8_t>(bytes[0]), decode_reference(bytes.subspan(8))};
}
std::array<char, page_header_bytes> encode_page_header(const PageHeader &header, Limits limits) {
	validate(header, limits);
	std::array<char, page_header_bytes> bytes{};
	constexpr std::string_view magic = "DTPAGE02";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 2, 4);
	write_integer(bytes, 12, static_cast<std::uint32_t>(header.kind), 4);
	write_integer(bytes, 16, header.length, 4);
	write_integer(bytes, 20, header.prefix, 4);
	write_integer(bytes, 24, header.children, 4);
	write_integer(bytes, 28, header.body, 4);
	write_integer(bytes, 32, header.keys, 8);
	put(bytes, 40, header.logical);
	return bytes;
}
PageHeader decode_page_header(std::span<const char> bytes, Limits limits) {
	require(bytes.size() >= page_header_bytes && std::string_view(bytes.data(), 8) == "DTPAGE02" &&
			read_integer(bytes, 8, 4) == 2 && !read_integer(bytes, 72, 8));
	PageHeader header;
	header.kind = static_cast<PageKind>(read_integer(bytes, 12, 4));
	header.length = read_integer(bytes, 16, 4);
	header.prefix = read_integer(bytes, 20, 4);
	header.children = read_integer(bytes, 24, 4);
	header.body = read_integer(bytes, 28, 4);
	header.keys = read_integer(bytes, 32, 8);
	copy(bytes, 40, header.logical);
	validate(header, limits);
	require(bytes.size() == header.length);
	return header;
}
std::array<char, root_bytes> encode_root(const Root &root, Limits limits) {
	validate(root, limits);
	std::array<char, root_bytes> bytes{};
	constexpr std::string_view magic = "DTROOT02";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 2, 4);
	write_integer(bytes, 12, root_bytes, 4);
	write_integer(bytes, 16, root.bytes, 8);
	write_integer(bytes, 24, root.records, 4);
	write_integer(bytes, 28, root.namespaces, 4);
	write_integer(bytes, 32, root.pages, 8);
	write_integer(bytes, 40, root.segments, 8);
	write_integer(bytes, 56, root.cursor.revision, 8);
	put(bytes, 64, root.cursor.cluster);
	put(bytes, 80, root.cursor.group);
	put(bytes, 96, root.cursor.restore_epoch);
	put(bytes, 112, root.cursor.scope);
	write_integer(bytes, 144, root.profile.semantic_id, 4);
	write_integer(bytes, 152, root.profile.required_capabilities, 8);
	if (root.pages)
		put(bytes, 160, encode_reference(root.tree));
	put(bytes, 248, root.logical);
	return bytes;
}
Root decode_root(std::span<const char> bytes, Limits limits) {
	require(bytes.size() == root_bytes && std::string_view(bytes.data(), 8) == "DTROOT02" &&
			read_integer(bytes, 8, 4) == 2 && read_integer(bytes, 12, 4) == root_bytes &&
			!read_integer(bytes, 48, 8) && !read_integer(bytes, 148, 4) &&
			std::all_of(bytes.begin() + 280, bytes.end(), [](char value) { return value == 0; }));
	Root root;
	root.bytes = read_integer(bytes, 16, 8);
	root.records = read_integer(bytes, 24, 4);
	root.namespaces = read_integer(bytes, 28, 4);
	root.pages = read_integer(bytes, 32, 8);
	root.segments = read_integer(bytes, 40, 8);
	root.cursor.revision = read_integer(bytes, 56, 8);
	copy(bytes, 64, root.cursor.cluster);
	copy(bytes, 80, root.cursor.group);
	copy(bytes, 96, root.cursor.restore_epoch);
	copy(bytes, 112, root.cursor.scope);
	root.profile.semantic_id = read_integer(bytes, 144, 4);
	root.profile.required_capabilities = read_integer(bytes, 152, 8);
	if (root.pages)
		root.tree = decode_reference(bytes.subspan(160, reference_bytes));
	else
		require(std::all_of(bytes.begin() + 160, bytes.begin() + 248, [](char value) { return value == 0; }));
	copy(bytes, 248, root.logical);
	validate(root, limits);
	return root;
}
} // namespace detent::local::paged
