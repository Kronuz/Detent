#include "detent_local/paged_layout.hh"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace p = detent::local::paged;
namespace l = detent::local;
static void require(bool ok) {
	if (!ok)
		throw std::runtime_error("paged framing invariant");
}
template <class F> static void rejects(F operation) {
	try {
		operation();
	} catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error("accepted corrupt paged framing");
}
int main() {
	std::array<char, 202> key{};
	auto length = p::encode_definition_key(key, "ns:knob");
	require(std::string_view(key.data(), length) == std::string_view("Kns\0knob\0", 9));
	length = p::encode_namespace_key(key, "ns");
	require(std::string_view(key.data(), length) == std::string_view("Wns\0", 4));
	rejects([&] { p::encode_definition_key(std::span<char>(key).first(2), "ns:knob"); });
	rejects([&] { p::encode_definition_key(key, "ns:knob:extra"); });
	rejects([&] { p::encode_namespace_key(key, std::string_view("ns\0extra", 8)); });
	p::Reference reference;
	reference.segment.fill('s');
	reference.physical.fill('p');
	reference.logical.fill('l');
	reference.offset = p::segment_bytes - p::page_bytes;
	reference.length = p::page_bytes;
	require(p::decode_reference(p::encode_reference(reference)) == reference);
	for (auto offset : {1u, p::segment_bytes, UINT32_MAX}) {
		auto bad = reference;
		bad.offset = offset;
		rejects([&] { p::encode_reference(bad); });
	}
	for (auto length : {0u, 79u, p::page_bytes + 1, UINT32_MAX}) {
		auto bad = reference;
		bad.length = length;
		rejects([&] { p::encode_reference(bad); });
	}
	auto child = p::encode_child({255, reference});
	require(p::decode_child(child).label == 255);
	child[7] = 1;
	rejects([&] { p::decode_child(child); });
	p::PageHeader header;
	header.kind = p::PageKind::Branch;
	header.children = 256;
	header.prefix = 512;
	header.keys = 500000;
	header.logical.fill('l');
	header.length = p::page_header_bytes + header.prefix + p::child_bytes * header.children;
	const auto encoded = p::encode_page_header(header);
	std::vector<char> page(header.length);
	std::copy(encoded.begin(), encoded.end(), page.begin());
	require(p::decode_page_header(page).children == 256);
	for (auto field : {8u, 12u, 16u, 20u, 24u, 28u, 72u}) {
		auto bad = page;
		bad[field] = static_cast<char>(255);
		rejects([&] { p::decode_page_header(bad); });
	}
	page.push_back(0);
	rejects([&] { p::decode_page_header(page); });
	page.resize(p::page_header_bytes - 1);
	rejects([&] { p::decode_page_header(page); });
	header.kind = p::PageKind::Namespace;
	header.children = header.prefix = 0;
	header.body = 8;
	header.keys = 1;
	header.length = p::page_header_bytes + 8;
	const auto terminal = p::encode_page_header(header);
	page.assign(header.length, 0);
	std::copy(terminal.begin(), terminal.end(), page.begin());
	require(p::decode_page_header(page).prefix == 0);
	p::Root root;
	root.cursor.cluster.fill('c');
	root.cursor.group.fill('g');
	root.cursor.restore_epoch.fill('e');
	root.cursor.scope.fill('s');
	root.logical = p::empty_commitment();
	require(p::decode_root(p::encode_root(root)).pages == 0);
	root.bytes = p::page_bytes + 2 * p::page_header_bytes;
	root.pages = 3;
	root.segments = root.records = root.namespaces = 1;
	root.tree = reference;
	root.logical = reference.logical;
	auto manifest = p::encode_root(root);
	require(p::decode_root(manifest).tree == reference);
	for (auto field : {8u, 12u, 16u, 24u, 28u, 32u, 40u, 48u, 144u, 148u, 152u, 280u, 319u}) {
		auto bad = manifest;
		bad[field] = static_cast<char>(255);
		rejects([&] { p::decode_root(bad); });
	}
	p::Limits short_limit;
	short_limit.bytes = p::page_bytes - 1;
	rejects([&] { p::decode_root(manifest, short_limit); });
	manifest[248] ^= 1;
	rejects([&] { p::decode_root(manifest); });
	std::array<char, 8> watermark{};
	l::write_integer(watermark, 0, 17, 8);
	header.logical = p::logical_commitment(p::PageKind::Namespace, {}, {}, watermark);
	const auto leaf_header = p::encode_page_header(header);
	page.assign(leaf_header.begin(), leaf_header.end());
	page.insert(page.end(), watermark.begin(), watermark.end());
	reference.offset = 0;
	reference.length = page.size();
	reference.logical = header.logical;
	reference.physical = p::physical_commitment(page);
	p::verify_page(page, reference);
	page.back() ^= 1;
	rejects([&] { p::verify_page(page, reference); });
	reference.physical = p::physical_commitment(page);
	rejects([&] { p::verify_page(page, reference); });
	std::array<p::Child, 2> children{{{0, reference}, {255, reference}}};
	const auto logical = p::logical_commitment(p::PageKind::Branch, "prefix", children, {});
	children[0].reference.segment.fill('x');
	children[0].reference.offset = 8;
	children[0].reference.physical.fill('x');
	require(logical == p::logical_commitment(p::PageKind::Branch, "prefix", children, {}));
	children[0].reference.logical[0] ^= 1;
	require(logical != p::logical_commitment(p::PageKind::Branch, "prefix", children, {}));
	children[1].label = 0;
	rejects([&] { p::logical_commitment(p::PageKind::Branch, "prefix", children, {}); });
	children[0].label = 255;
	rejects([&] { p::logical_commitment(p::PageKind::Branch, "prefix", children, {}); });
	std::cout << "paged root/page/extent framing and corruption boundaries passed\n";
}
