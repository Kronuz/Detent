#include "paged_canonical_copy.hh"
#include "paged_navigation.hh"
#include "paged_segment_builder.hh"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool ok) {
	if (!ok)
		throw std::invalid_argument("invalid canonical copy source");
}
// A page waiting for its seal: its source and its children's new references, by external index.
struct Pending {
	p::Reference source, placed;
	std::uint16_t index = 0;
	std::vector<p::Reference> children;
};
// D5's seal order among the children a node seals: lower first, then larger, then label order.
bool seals_first(const Pending &a, const Pending &b) {
	return p::seals_before(a.source.height - 1u, a.source.length - p::page_header_bytes,
						   b.source.height - 1u, b.source.length - p::page_header_bytes);
}
struct Copier {
	std::shared_ptr<d::StorageLedger> account;
	std::span<const SegmentInput> inputs;
	GenerationId generation;
	p::Limits limits;
	CanonicalCopy out;
	std::optional<SegmentBuilder> segment;
	std::uint64_t pages = 0, bytes = 0;

	std::span<const char> read(const p::Reference &r) const {
		p::validate(r);
		const auto i = find_segment(inputs, r.segment);
		require(i != inputs.size());
		const auto segment_bytes = inputs[i].bytes;
		require(r.offset <= segment_bytes.size() && r.length <= segment_bytes.size() - r.offset);
		const auto page = segment_bytes.subspan(r.offset, r.length);
		const auto h = p::decode_page_header(page, limits);
		require(h.length == r.length && h.keys == r.keys && h.height == r.height &&
				h.digest == r.digest);
		return page;
	}
	void run(SegmentBuilder &builder) {
		ValidationBudget budget{std::numeric_limits<std::size_t>::max(),
								std::numeric_limits<std::size_t>::max()};
		builder.step(budget);
	}
	void close() {
		segment->seal();
		run(*segment);
		require(segment->done() && out.segments.size() < limits.segments);
		out.segments.push_back({segment->ownership(), segment->bytes()});
		segment.reset();
	}
	// Writes a page whose children are placed, at the build's next offset (dense-pages D5.1).
	void emit(Pending &page) {
		const auto source = read(page.source);
		auto buffer = d::make_owned_buffer(account, source.size());
		buffer->append({source.data(), source.size()});
		auto target = buffer->mutable_bytes();
		const auto count = page.children.size();
		for (std::size_t i = 0; i < count; ++i) {
			require(page.children[i] != p::Reference{});
			const auto placement = p::encode_placement(page.children[i]);
			std::copy(placement.begin(), placement.end(),
					  target.end() - p::placement_bytes * (count - i));
		}
		buffer->seal();
		const SealedPage sealed{buffer, page.source, {}};
		if (segment && !segment->fits(sealed))
			close();
		if (!segment)
			segment.emplace(account, generation, out.segments.size());
		segment->append(sealed);
		run(*segment);
		require(segment->page_ready() && pages < limits.pages);
		page.placed = segment->reference();
		page.children = {};
		++pages;
		bytes += page.placed.length;
	}
	// Visits a node in postorder; at its end it seals the children it packed into their own pages.
	void node(Pending &page, std::span<const char> source, p::NodeAt at, unsigned depth) {
		require(depth < 203);
		const auto n = at.decode(source, limits);
		std::vector<Pending> sealed;
		for (std::size_t i = 0; i < n.labels.size(); ++i) {
			const auto slot = n.slot(i);
			if (!(slot & 0x8000)) {
				node(page, source, p::NodeAt::child(n, slot), depth + 1);
				continue;
			}
			const std::uint16_t index = slot & 0x7fff;
			require(index < page.children.size() && page.children[index] == p::Reference{});
			auto child = visit(p::decode_external(source, index, limits), depth + 1);
			if (child.placed != p::Reference{})
				page.children[index] = child.placed;
			else {
				child.index = index;
				sealed.push_back(std::move(child));
			}
		}
		std::stable_sort(sealed.begin(), sealed.end(), seals_first);
		for (auto &child : sealed) {
			emit(child);
			page.children[child.index] = child.placed;
		}
	}
	// A page above capacity was sealed alone, at the end of its top node; any other page is
	// sealed by its parent's node or, for the root, last.
	Pending visit(const p::Reference &r, unsigned depth) {
		const auto source = read(r);
		Pending page{r, {}, 0, std::vector<p::Reference>(p::decode_page_header(source, limits).externals)};
		node(page, source, p::NodeAt{}, depth);
		if (r.length > p::page_capacity)
			emit(page);
		return page;
	}
};
} // namespace
CanonicalCopy canonical_copy(std::shared_ptr<d::StorageLedger> account,
							 std::span<const SegmentInput> inputs, const p::Root &root,
							 const GenerationId &generation, p::Limits limits) {
	Copier copier{std::move(account), inputs, generation, limits, {root, {}}, {}, 0, 0};
	if (root.pages) {
		auto tree = copier.visit(root.tree, 0);
		if (tree.placed == p::Reference{})
			copier.emit(tree);
		copier.close();
		copier.out.root.tree = tree.placed;
	}
	require(copier.pages == root.pages && copier.bytes == root.bytes);
	copier.out.root.segments = copier.out.segments.size();
	return std::move(copier.out);
}
} // namespace detent::local::detail
