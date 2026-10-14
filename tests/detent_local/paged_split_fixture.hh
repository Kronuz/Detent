#pragma once
#include "paged_navigation.hh"
#include "paged_segment_builder.hh"
#include "paged_snapshot_builder.hh"
#include <functional>
namespace detent::local::testing {
// Separates index pages from leaves for physical relocation fixtures.
struct SplitPagedInput {
	paged::Root root;
	std::array<detail::BuiltSegment, 2> packs;
	std::array<detail::SegmentInput, 2> inputs;
	SplitPagedInput(std::shared_ptr<distribution::StorageLedger> heap,
					std::span<const detail::SegmentInput> original, paged::Root source,
					StorageIdentity storage, GenerationId generation)
		: root(source) {
		detail::SegmentBuilder branches(heap, storage, generation, 0), leaves(heap, storage, generation, 1);
		std::function<paged::Reference(paged::Reference)> copy = [&](paged::Reference reference) {
			const auto index = detail::find_segment(original, reference.segment);
			if (index == original.size())
				throw std::logic_error("split fixture segment missing");
			const auto bytes = detail::checked_page_bytes(original[index], reference);
			const auto header = paged::decode_page_header(bytes);
			const std::string_view prefix(bytes.data() + paged::page_header_bytes, header.prefix);
			const auto body = bytes.subspan(paged::page_header_bytes + header.prefix, header.body);
			std::vector<paged::Child> children;
			for (std::uint32_t i = 0; i < header.children; ++i) {
				const auto child = paged::decode_child(bytes.subspan(
					paged::page_header_bytes + header.prefix + i * paged::child_bytes, paged::child_bytes));
				children.push_back({child.label, copy(child.reference)});
			}
			detail::PageBuilder page(heap, header.kind, header.keys, prefix, children, body);
			while (!page.done()) {
				ValidationBudget budget;
				page.step(budget);
			}
			auto &pack = header.kind == paged::PageKind::Branch ? branches : leaves;
			pack.append(page);
			while (!pack.page_ready()) {
				ValidationBudget budget;
				pack.step(budget);
			}
			const auto result = pack.reference();
			if (result.logical != reference.logical)
				throw std::logic_error("split fixture changed semantics");
			return result;
		};
		if (!source.pages)
			throw std::invalid_argument("split fixture requires a nonempty tree");
		root.tree = copy(source.tree);
		branches.seal();
		leaves.seal();
		while (!branches.done() || !leaves.done()) {
			ValidationBudget first, second;
			if (!branches.done())
				branches.step(first);
			if (!leaves.done())
				leaves.step(second);
		}
		packs[0] = {branches.ownership(), branches.bytes()};
		packs[1] = {leaves.ownership(), leaves.bytes()};
		root.segments = 2;
		for (std::size_t i = 0; i < inputs.size(); ++i)
			inputs[i] = {packs[i].ownership, {packs[i].bytes->data(), packs[i].bytes->size()}};
		std::sort(inputs.begin(), inputs.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
	}
};
} // namespace detent::local::testing
