#pragma once
#include "paged_navigation.hh"
#include "paged_segment_builder.hh"
#include "paged_snapshot_builder.hh"
#include <functional>
namespace detent::local::testing {
// Separates pages with external edges from terminal pages for relocation.
struct SplitPagedInput {
	paged::Root root;
	std::array<detail::BuiltSegment, 2> packs;
	std::array<detail::SegmentInput, 2> inputs;
	SplitPagedInput(std::shared_ptr<distribution::StorageLedger> heap,
					std::span<const detail::SegmentInput> original, paged::Root source,
					StorageIdentity storage, GenerationId generation)
		: root(source) {
		detail::SegmentBuilder branches(heap, storage, generation, 0),
			leaves(heap, storage, generation, 1);
		std::function<paged::Reference(paged::Reference)> copy = [&](paged::Reference reference) {
			const auto index = detail::find_segment(original, reference.segment);
			if (index == original.size())
				throw std::logic_error("split fixture segment missing");
			const auto bytes = detail::checked_page_bytes(original[index], reference);
			const auto h = paged::decode_page_header(bytes);
			auto buffer = distribution::make_owned_buffer(heap, bytes.size());
			buffer->append({bytes.data(), bytes.size()});
			for (std::size_t i = 0; i < h.externals; ++i) {
				const auto child = copy(paged::decode_external(bytes, i));
				const auto placement = paged::encode_placement(child);
				std::copy(placement.begin(), placement.end(),
						  buffer->mutable_bytes().end() -
							  paged::placement_bytes * (h.externals - i));
			}
			buffer->seal();
			detail::SealedPage page{buffer, reference, {}};
			auto &pack = h.externals ? branches : leaves;
			pack.append(page);
			while (!pack.page_ready()) {
				ValidationBudget b;
				pack.step(b);
			}
			const auto result = pack.reference();
			if (result.digest != reference.digest)
				throw std::logic_error("split fixture changed semantics");
			return result;
		};
		if (source.pages < 2)
			throw std::invalid_argument("split fixture requires several dense pages");
		root.tree = copy(source.tree);
		branches.seal();
		leaves.seal();
		while (!branches.done() || !leaves.done()) {
			ValidationBudget a, b;
			if (!branches.done())
				branches.step(a);
			if (!leaves.done())
				leaves.step(b);
		}
		packs[0] = {branches.ownership(), branches.bytes()};
		packs[1] = {leaves.ownership(), leaves.bytes()};
		root.segments = 2;
		for (std::size_t i = 0; i < inputs.size(); ++i)
			inputs[i] = {packs[i].ownership, {packs[i].bytes->data(), packs[i].bytes->size()}};
		std::sort(inputs.begin(), inputs.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(),
							   a.ownership.segment.size()) < 0;
		});
	}
};
} // namespace detent::local::testing
