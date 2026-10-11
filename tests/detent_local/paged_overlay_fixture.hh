#pragma once
#include "paged_navigation.hh"
#include "paged_trie_overlay.hh"
#include <vector>
namespace p = detent::local::paged;
namespace l = detent::local;
inline std::pair<p::Digest, std::uint64_t> fingerprint(l::detail::OverlayReference ref,
													   std::span<const l::detail::SegmentInput> segments) {
	if (!ref)
		return {p::empty_commitment(), 0};
	if (ref.old) {
		const auto r = p::decode_reference({ref.old, p::reference_bytes});
		const auto bytes =
			l::detail::checked_page_bytes(segments[l::detail::find_segment(segments, r.segment)], r);
		return {r.logical, p::decode_page_header(bytes).keys};
	}
	const auto &node = *ref.fresh;
	std::vector<p::Child> children;
	std::uint64_t keys = 0;
	for (const auto &child : node.children) {
		const auto [logical, count] = fingerprint(child.reference, segments);
		p::Reference r;
		r.logical = logical;
		children.push_back({child.label, r});
		keys += count;
	}
	if (node.kind != p::PageKind::Branch)
		keys = 1;
	require(node.keys == keys);
	return {p::logical_commitment(node.kind, node.prefix, children, node.body), keys};
}
