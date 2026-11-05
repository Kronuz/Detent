#include "detent_local/paged_layout.hh"
#include "detent/validation.hh"
#include "detent_local/read_counters.hh"
#include <algorithm>
#include <atomic>
#include <sha256.h>
#include <stdexcept>

namespace detent::local::paged {
namespace {
void require(bool condition) {
	if (!condition)
		throw std::invalid_argument("invalid paged local image");
}
bool zero(std::span<const char> bytes) {
	return std::all_of(bytes.begin(), bytes.end(), [](char byte) { return byte == 0; });
}
template <std::size_t N>
void copy(std::span<const char> bytes, std::size_t offset, std::array<char, N> &out) {
	require(offset <= bytes.size() && N <= bytes.size() - offset);
	std::copy_n(bytes.begin() + offset, N, out.begin());
}
template <std::size_t N>
void put(std::span<char> bytes, std::size_t offset, const std::array<char, N> &value) {
	require(offset <= bytes.size() && N <= bytes.size() - offset);
	std::copy(value.begin(), value.end(), bytes.begin() + offset);
}
void validate_summary(const Reference &r) {
	require(r.length >= minimum_page_bytes && r.length <= page_bytes && r.keys && r.height);
}
void validate(const Reference &r) {
	if (r == Reference{})
		return;
	validate_summary(r);
	require(!zero(r.segment) && r.offset % 8 == 0 && r.offset <= segment_bytes &&
			r.length <= segment_bytes - r.offset);
}
void validate(const PageHeader &h, Limits limits) {
	require(h.length >= minimum_page_bytes && h.length <= page_bytes && h.length <= limits.bytes &&
			h.nodes && h.keys && h.keys <= std::uint64_t(limits.records) + limits.namespaces &&
			h.height && h.externals < 0x8000 &&
			h.externals <= (h.length - page_header_bytes) / child_bytes &&
			h.nodes <= (h.length - page_header_bytes - child_bytes * h.externals) / 4);
}
void validate(const Root &root, Limits limits) {
	Header context;
	context.cursor = root.cursor;
	context.profile = root.profile;
	encode_header(context);
	require(root.records <= limits.records && root.namespaces <= limits.namespaces &&
			root.pages <= limits.pages && root.segments <= limits.segments &&
			root.bytes <= limits.bytes);
	const auto keys = std::uint64_t(root.records) + root.namespaces;
	if (!root.pages) {
		require(!keys && !root.bytes && !root.segments && root.tree == Reference{});
	} else {
		validate(root.tree);
		require(keys && root.tree.keys == keys && root.segments && root.segments <= root.pages &&
				root.pages < 2 * keys && root.pages <= root.bytes / minimum_page_bytes &&
				root.tree.length <= root.bytes &&
				root.pages - 1 <= (root.bytes - root.tree.length) / minimum_page_bytes);
	}
}
Digest finish(SHA256 &hash) {
	Digest result;
	hash.getHash(reinterpret_cast<unsigned char *>(result.data()));
	return result;
}
std::atomic<std::uint64_t> digest_count{0};
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
std::uint64_t page_digests() noexcept { return digest_count.load(std::memory_order_relaxed); }
void count_page_digest() noexcept { digest_count.fetch_add(1, std::memory_order_relaxed); }
Digest page_commitment(std::span<const char> bytes, Limits limits) {
	const auto h = decode_page_header(bytes, limits);
	count_page_digest();
	const auto end = bytes.size() - placement_bytes * h.externals;
	DETENT_COUNT(bytes_hashed, end - 32);
	SHA256 hash;
	hash.add(bytes.data(), 32);
	hash.add(bytes.data() + 64, end - 64);
	return finish(hash);
}
Digest empty_commitment() {
	SHA256 hash;
	constexpr std::string_view domain = "detent.local.empty.v3";
	hash.add(domain.data(), domain.size());
	return finish(hash);
}
Digest state_commitment(const Root &root) {
	return root.tree == Reference{} ? empty_commitment() : root.tree.digest;
}
std::array<char, reference_bytes> encode_reference(const Reference &r) {
	validate(r);
	std::array<char, reference_bytes> bytes{};
	put(bytes, 0, r.segment);
	write_integer(bytes, 16, r.offset, 4);
	write_integer(bytes, 20, r.length, 4);
	write_integer(bytes, 24, r.keys, 8);
	bytes[32] = static_cast<char>(r.height);
	put(bytes, 33, r.digest);
	return bytes;
}
Reference decode_reference(std::span<const char> bytes) {
	require(bytes.size() == reference_bytes);
	Reference r;
	copy(bytes, 0, r.segment);
	r.offset = read_integer(bytes, 16, 4);
	r.length = read_integer(bytes, 20, 4);
	r.keys = read_integer(bytes, 24, 8);
	r.height = static_cast<unsigned char>(bytes[32]);
	copy(bytes, 33, r.digest);
	validate(r);
	return r;
}
std::array<char, content_bytes> encode_content(const Reference &r) {
	validate_summary(r);
	std::array<char, content_bytes> bytes{};
	put(bytes, 0, r.digest);
	write_integer(bytes, 32, r.keys, 8);
	write_integer(bytes, 40, r.length, 4);
	bytes[44] = static_cast<char>(r.height);
	return bytes;
}
std::array<char, placement_bytes> encode_placement(const Reference &r) {
	validate(r);
	require(r != Reference{});
	std::array<char, placement_bytes> bytes{};
	put(bytes, 0, r.segment);
	write_integer(bytes, 16, r.offset, 4);
	return bytes;
}
Reference decode_external(std::span<const char> bytes, std::uint16_t index, Limits limits) {
	const auto h = decode_page_header(bytes, limits);
	require(index < h.externals);
	const auto content = bytes.size() - child_bytes * h.externals + content_bytes * index;
	const auto placement = bytes.size() - placement_bytes * h.externals + placement_bytes * index;
	Reference r;
	copy(bytes, content, r.digest);
	r.keys = read_integer(bytes, content + 32, 8);
	r.length = read_integer(bytes, content + 40, 4);
	r.height = static_cast<unsigned char>(bytes[content + 44]);
	require(zero(bytes.subspan(content + 45, 3)));
	copy(bytes, placement, r.segment);
	r.offset = read_integer(bytes, placement + 16, 4);
	validate(r);
	require(r != Reference{});
	return r;
}
std::array<char, page_header_bytes> encode_page_header(const PageHeader &h, Limits limits) {
	validate(h, limits);
	std::array<char, page_header_bytes> bytes{};
	constexpr std::string_view magic = "DTPAGE03";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, h.length, 4);
	write_integer(bytes, 12, h.externals, 2);
	write_integer(bytes, 14, h.nodes, 2);
	write_integer(bytes, 16, h.keys, 8);
	bytes[24] = static_cast<char>(h.height);
	put(bytes, 32, h.digest);
	return bytes;
}
PageHeader decode_page_header(std::span<const char> bytes, Limits limits) {
	require(bytes.size() >= page_header_bytes && std::string_view(bytes.data(), 8) == "DTPAGE03" &&
			zero(bytes.subspan(25, 7)));
	PageHeader h;
	h.length = read_integer(bytes, 8, 4);
	h.externals = read_integer(bytes, 12, 2);
	h.nodes = read_integer(bytes, 14, 2);
	h.keys = read_integer(bytes, 16, 8);
	h.height = static_cast<unsigned char>(bytes[24]);
	copy(bytes, 32, h.digest);
	validate(h, limits);
	require(bytes.size() == h.length);
	return h;
}
std::uint16_t NodeView::slot(std::size_t index) const {
	require(index < labels.size());
	return read_integer(slots, 2 * index, 2);
}
NodeView decode_node(std::span<const char> bytes, std::size_t offset, Limits limits) {
	const auto h = decode_page_header(bytes, limits);
	const auto end = bytes.size() - child_bytes * h.externals;
	require(offset >= page_header_bytes && offset <= end && end - offset >= 3);
	const auto kind = static_cast<PageKind>(bytes[offset]);
	const auto prefix = static_cast<unsigned char>(bytes[offset + 1]);
	require(prefix <= 202 && prefix <= limits.prefix_bytes);
	if (kind == PageKind::Branch) {
		const std::size_t count = static_cast<unsigned char>(bytes[offset + 2]) + 1;
		require(count >= 2 && prefix <= end - offset - 3 &&
				count <= (end - offset - 3 - prefix) / 3);
		const auto labels = bytes.subspan(offset + 3 + prefix, count);
		int previous = -1;
		for (auto label : labels) {
			require(static_cast<unsigned char>(label) > previous);
			previous = static_cast<unsigned char>(label);
		}
		return {kind,
				{bytes.data() + offset + 3, prefix},
				{},
				labels,
				bytes.subspan(offset + 3 + prefix + count, 2 * count),
				offset + 3 + prefix + 3 * count};
	}
	require((kind == PageKind::Definition || kind == PageKind::Namespace) && end - offset >= 4);
	const auto body = read_integer(bytes, offset + 2, 2);
	require(body && (kind != PageKind::Namespace || body == 8) &&
			body <= page_bytes - 80 - prefix && prefix <= end - offset - 4 &&
			body <= end - offset - 4 - prefix);
	return {kind, {bytes.data() + offset + 4, prefix}, bytes.subspan(offset + 4 + prefix, body), {},
			{},	  offset + 4 + prefix + body};
}
void verify_page(std::span<const char> bytes, const Reference &reference, Limits limits) {
	validate(reference);
	require(reference != Reference{});
	const auto h = decode_page_header(bytes, limits);
	require(h.length == reference.length && h.keys == reference.keys &&
			h.height == reference.height && h.digest == reference.digest &&
			page_commitment(bytes, limits) == reference.digest);
	struct Frame {
		NodeView node;
		std::size_t child = 0, depth = 0;
	};
	std::array<Frame, 203> stack{};
	std::size_t depth = 0, position = page_header_bytes, nodes = 0, external = 0, key_depth = 0;
	std::uint64_t keys = 0;
	unsigned height = 1;
	bool visit = true;
	for (;;) {
		if (visit) {
			const auto node = decode_node(bytes, position, limits);
			require(++nodes <= h.nodes && key_depth <= 202 &&
					node.prefix.size() <= 202 - key_depth);
			position = node.end;
			if (node.kind == PageKind::Branch) {
				require(depth < stack.size() && key_depth + node.prefix.size() < 202);
				stack[depth++] = {node, 0, key_depth + node.prefix.size() + 1};
			} else {
				require(keys < h.keys);
				++keys;
			}
		}
		visit = false;
		while (depth) {
			auto &frame = stack[depth - 1];
			if (frame.child == frame.node.labels.size()) {
				--depth;
				continue;
			}
			const auto slot = frame.node.slot(frame.child++);
			if (slot & 0x8000) {
				require((slot & 0x7fff) == external && external < h.externals);
				const auto r = decode_external(bytes, external++, limits);
				require(r.keys <= h.keys - keys && r.height < 255);
				keys += r.keys;
				height = std::max(height, unsigned(r.height) + 1);
				continue;
			}
			require(slot == position);
			key_depth = frame.depth;
			visit = true;
			break;
		}
		if (!visit)
			break;
	}
	require(position == bytes.size() - child_bytes * h.externals && nodes == h.nodes &&
			external == h.externals && keys == h.keys && height == h.height);
}
std::array<char, root_bytes> encode_root(const Root &root, Limits limits) {
	validate(root, limits);
	std::array<char, root_bytes> bytes{};
	constexpr std::string_view magic = "DTROOT03";
	std::copy(magic.begin(), magic.end(), bytes.begin());
	write_integer(bytes, 8, 3, 4);
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
	put(bytes, 160, encode_reference(root.tree));
	return bytes;
}
Root decode_root(std::span<const char> bytes, Limits limits) {
	require(bytes.size() == root_bytes && std::string_view(bytes.data(), 8) == "DTROOT03" &&
			read_integer(bytes, 8, 4) == 3 && read_integer(bytes, 12, 4) == root_bytes &&
			zero(bytes.subspan(48, 8)) && zero(bytes.subspan(148, 4)) && zero(bytes.subspan(225)));
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
	root.tree = decode_reference(bytes.subspan(160, reference_bytes));
	validate(root, limits);
	return root;
}
} // namespace detent::local::paged
