#pragma once
#include "paged_navigation.hh"
#include "paged_packer.hh"
#include "paged_trie_overlay.hh"
#include <functional>
namespace p = detent::local::paged;
namespace l = detent::local;
inline std::pair<p::Digest, std::uint64_t>
fingerprint(l::detail::OverlayReference root, std::span<const l::detail::SegmentInput> segments) {
	if (!root)
		return {p::empty_commitment(), 0};
	if (root.fresh && !root.fresh->relocation_bytes.empty())
		return {root.fresh->relocation_reference.digest, root.fresh->keys};
	auto account = std::make_shared<detent::distribution::StorageLedger>(32 * 1024 * 1024);
	l::detail::PagedPacker packer(account);
	std::uint64_t ordinal = 0;
	auto drain = [&] {
		while (!packer.accepted() && !packer.done()) {
			l::ValidationBudget b;
			packer.step(b);
			if (packer.ready()) {
				auto r = packer.page().summary;
				r.segment.fill('f');
				l::write_integer(r.segment, 8, ++ordinal, 8);
				l::ValidationBudget commit;
				require(packer.commit(r, commit));
			}
		}
	};
	std::function<void(l::detail::OverlayReference, std::string)> visit = [&](auto ref,
																			  std::string path) {
		if (ref.old) {
			const auto &r = *ref.old;
			const auto bytes = l::detail::checked_page_bytes(
				segments[l::detail::find_segment(segments, r.segment)], r);
			if (ref.at.first() && r.length > p::page_capacity)
				packer.push_reference(r, path);
			else
				packer.push_cluster(bytes, ref.at, path);
			drain();
			return;
		}
		const auto &n = *ref.fresh;
		const auto start = path.size();
		path += n.prefix;
		std::vector<std::uint8_t> labels;
		for (const auto &child : n.children) {
			visit(child.reference, path + char(child.label));
			labels.push_back(child.label);
		}
		packer.push({n.kind, 0, n.keys, path, std::string_view(path).substr(start), labels},
					n.body);
		drain();
	};
	visit(root, {});
	packer.finish();
	drain();
	const auto r = packer.root();
	return {r.digest, r.keys};
}
