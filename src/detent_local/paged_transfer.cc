#include "paged_transfer.hh"
#include "detent_local/layout.hh"
#include "detent_local/paged_definition.hh"
#include "paged_node_step.hh"
#include "paged_page_validator.hh"
#include <algorithm>
#include <map>
#include <string>

namespace detent::local::detail {
namespace p = paged;
namespace {
[[noreturn]] void refuse(TransferRefusal reason, const char *what) { throw TransferRefused(reason, what); }
bool zero(std::span<const char> bytes) {
	return std::all_of(bytes.begin(), bytes.end(), [](char byte) { return byte == 0; });
}
std::size_t content_at(std::size_t length, std::size_t externals, std::size_t j) {
	return length - p::child_bytes * externals + p::content_bytes * j;
}
std::size_t placement_at(std::size_t length, std::size_t externals, std::size_t j) {
	return length - p::placement_bytes * externals + p::placement_bytes * j;
}
// dense-pages D4.4: a definition's header against the revision, a watermark at most the revision.
void check_leaves(std::span<const char> page, const p::PageHeader &header, const TransferTarget &target, p::Limits limits) {
	const auto end = page.size() - p::child_bytes * header.externals;
	for (p::NodeAt at; at.offset() < end;) {
		const auto node = at.decode(page, limits);
		try {
			if (node.kind == p::PageKind::Definition)
				p::validate_definition(node.body, target.revision, target.maximum_overrides);
			else if (node.kind == p::PageKind::Namespace && read_integer(node.body, 0, 8) > target.revision)
				refuse(TransferRefusal::Leaf, "watermark past the frame's revision");
		} catch (const TransferRefused &) {
			throw;
		} catch (const std::invalid_argument &) {
			refuse(TransferRefusal::Leaf, "definition invalid at the frame's revision");
		}
		at = p::NodeAt::after(node);
	}
}
constexpr bool before(const TransferPlace &a, const TransferPlace &b) {
	return a.segment < b.segment || (a.segment == b.segment && a.offset < b.offset);
}

// One receive: the pages laid out in their segments, the walk's resolutions and the seal order.
struct Receiver {
	struct Resolved {
		std::size_t child = SIZE_MAX; // a transfer page, or SIZE_MAX for a base child
		p::Reference base;
	};
	struct Sealed {
		unsigned height;
		std::size_t size, label, child;
	};
	const TransferBase *base;
	p::Limits limits;
	std::vector<std::span<char>> pages;
	std::vector<p::PageHeader> headers;
	std::vector<TransferPlace> places;
	std::vector<std::size_t> first, next_external; // per page: its first resolution, its next slot
	std::vector<Resolved> resolved;
	std::vector<bool> used;
	std::vector<std::size_t> order;
	std::string path;
	std::uint64_t base_children = 0, base_walked = 0;

	std::size_t last() const { return pages.size() - 1; }
	std::size_t resolve(std::size_t i, std::size_t j) {
		const auto page = pages[i];
		const auto at = placement_at(page.size(), headers[i].externals, j);
		if (!zero(page.subspan(at, 12)))
			refuse(TransferRefusal::Layout, "placement names no transfer segment");
		const TransferPlace place{std::uint32_t(read_integer(page, at + 12, 4)),
								  std::uint32_t(read_integer(page, at + 16, 4))};
		auto &out = resolved[first[i] + j];
		if (place.segment || !base) {
			const auto found = std::lower_bound(places.begin(), places.end(), place, before);
			if (found == places.end() || *found != place)
				refuse(TransferRefusal::Layout, "placement names no page of the derived layout");
			const auto child = std::size_t(found - places.begin());
			if (child >= i)
				refuse(TransferRefusal::ParentFirst, "child not before its parent");
			if (used[child])
				refuse(TransferRefusal::Twice, "page referenced twice");
			used[child] = true;
			out.child = child;
			return child;
		}
		if (!zero(page.subspan(at, p::placement_bytes)))
			refuse(TransferRefusal::Layout, "placement names no page of the derived layout");
		out.base = find_base();
		++base_children;
		return SIZE_MAX;
	}
	// The base page at `path`: the walk follows prefixes and labels from the base's root.
	p::Reference find_base() {
		const auto lacks = [] { refuse(TransferRefusal::BaseLacks, "base lacks a page the delta reuses"); };
		auto reference = base->root;
		if (reference == p::Reference{})
			lacks();
		try {
			std::size_t consumed = 0;
			auto page = base->pages(reference);
			p::NodeAt at;
			for (std::uint64_t walked = 1;;) {
				if (page.size() != reference.length)
					lacks();
				const auto node = at.decode(page, limits);
				const auto prefix = node.prefix;
				if (path.size() - consumed < prefix.size() ||
					!std::equal(prefix.begin(), prefix.end(), path.begin() + consumed))
					lacks();
				consumed += prefix.size();
				if (node.kind != p::PageKind::Branch || consumed == path.size())
					lacks();
				const auto label = path[consumed++];
				const auto found = std::find(node.labels.begin(), node.labels.end(), label);
				if (found == node.labels.end())
					lacks();
				const auto slot = node.slot(std::size_t(found - node.labels.begin()));
				if (!(slot & 0x8000)) {
					at = p::NodeAt::child(node, slot);
					continue;
				}
				reference = p::decode_external(page, slot & 0x7fff, limits);
				++walked;
				if (consumed == path.size()) {
					base_walked += walked;
					return reference;
				}
				page = base->pages(reference);
				at = {};
			}
		} catch (const TransferRefused &) {
			throw;
		} catch (const std::invalid_argument &) {
			refuse(TransferRefusal::BaseLacks, "base page unreadable on the path");
		}
	}
	// D9's walk: resolves the node's external slots and emits, after its subtrees, the transfer
	// pages it seals in D5's order; a page over the capacity follows its own top node.
	p::NodeAt visit(std::size_t i, p::NodeAt at, bool top) {
		const auto page = pages[i];
		const auto node = at.decode(page, limits);
		const bool alone = top && page.size() > p::page_capacity && i != last();
		auto next = p::NodeAt::after(node);
		if (node.kind != p::PageKind::Branch) {
			if (path.size() + node.prefix.size() > 202)
				refuse(TransferRefusal::Page, "key longer than 202 bytes");
			if (alone)
				order.push_back(i);
			return next;
		}
		if (path.size() + node.prefix.size() >= 202)
			refuse(TransferRefusal::Page, "key longer than 202 bytes");
		const auto restore = path.size();
		path.append(node.prefix);
		std::vector<Sealed> here;
		for (std::size_t c = 0; c < node.labels.size(); ++c) {
			path.push_back(node.labels[c]);
			const auto slot = node.slot(c);
			if (slot & 0x8000) {
				const auto j = std::size_t(slot & 0x7fff);
				if (j != next_external[i] || j >= headers[i].externals)
					refuse(TransferRefusal::Page, "external slots out of order");
				++next_external[i];
				const auto entry = content_at(page.size(), headers[i].externals, j);
				const auto length = read_integer(page, entry + 40, 4);
				const auto height = static_cast<unsigned char>(page[entry + 44]);
				const auto child = resolve(i, j);
				if (child != SIZE_MAX) {
					visit(child, {}, true);
					if (length <= p::page_capacity)
						here.push_back({height, std::size_t(length) - p::page_header_bytes, c, child});
				}
			} else {
				const auto inner = p::NodeAt::child(node, slot);
				if (inner != next)
					refuse(TransferRefusal::Page, "an in-page slot off its preorder offset");
				next = visit(i, inner, false);
			}
			path.pop_back();
		}
		path.resize(restore);
		std::stable_sort(here.begin(), here.end(), [](const Sealed &a, const Sealed &b) {
			return p::seals_before(a.height, a.size, b.height, b.size);
		});
		for (const auto &sealed : here)
			order.push_back(sealed.child);
		if (alone)
			order.push_back(i);
		return next;
	}
};
} // namespace

std::vector<TransferPlace> transfer_layout(std::span<const std::uint32_t> lengths) {
	std::vector<TransferPlace> out;
	out.reserve(lengths.size());
	std::uint32_t number = 0;
	std::size_t used = p::segment_bytes;
	for (const auto length : lengths) {
		auto start = (used + 7) & ~std::size_t(7);
		if (start + length > p::segment_bytes) {
			++number;
			start = 0;
		}
		out.push_back({number, std::uint32_t(start)});
		used = start + length;
	}
	return out;
}

std::vector<char> make_transfer(std::span<const p::Reference> pages, const TransferPages &read, p::Limits limits) {
	std::vector<std::uint32_t> lengths;
	lengths.reserve(pages.size());
	for (const auto &page : pages)
		lengths.push_back(page.length);
	const auto places = transfer_layout(lengths);
	std::map<std::pair<SegmentId, std::uint32_t>, std::size_t> where;
	for (std::size_t i = 0; i < pages.size(); ++i)
		where.emplace(std::pair{pages[i].segment, pages[i].offset}, i);
	std::vector<char> out;
	for (std::size_t i = 0; i < pages.size(); ++i) {
		const auto bytes = read(pages[i]);
		if (bytes.size() != pages[i].length)
			throw std::logic_error("transfer page of another length");
		const auto header = p::decode_page_header(bytes, limits);
		const auto start = out.size();
		out.insert(out.end(), bytes.begin(), bytes.end());
		const std::span<char> page(out.data() + start, bytes.size());
		for (std::size_t j = 0; j < header.externals; ++j) {
			const auto child = p::decode_external(bytes, std::uint16_t(j), limits);
			const auto content = content_at(page.size(), header.externals, j);
			const auto placement = placement_at(page.size(), header.externals, j);
			std::fill_n(page.begin() + placement, p::placement_bytes, 0);
			const auto found = where.find({child.segment, child.offset});
			if (found == where.end())
				continue;
			if (found->second >= i)
				throw std::logic_error("transfer child after its parent");
			write_integer(page, placement + 12, places[found->second].segment, 4);
			write_integer(page, placement + 16, places[found->second].offset, 4);
			std::fill_n(page.begin() + content, 32, 0);
		}
		std::fill_n(page.begin() + 32, 32, 0);
	}
	return out;
}

ReceivedTransfer receive_transfer(std::shared_ptr<distribution::StorageLedger> account, std::span<const char> stream,
								  const TransferBase *base, const TransferTarget &target,
								  const TransferSegmentId &segment_id, p::Limits limits) {
	ReceivedTransfer out;
	if (stream.empty()) {
		if (!base && target.root == p::empty_commitment())
			return out;
		if (base && p::state_commitment(p::Root{.tree = base->root}) == target.root) {
			out.tree = base->root;
			return out;
		}
		refuse(TransferRefusal::Empty, "empty transfer");
	}
	// D4.1: split and lay out.
	std::vector<std::uint32_t> lengths;
	std::uint64_t total = 0;
	for (std::size_t at = 0; at < stream.size();) {
		if (stream.size() - at < 12 || std::string_view(stream.data() + at, 8) != "DTPAGE03")
			refuse(TransferRefusal::Framing, "bytes outside a page");
		const auto length = read_integer(stream, at + 8, 4);
		if (length < p::minimum_page_bytes || length > p::page_bytes || length > stream.size() - at)
			refuse(TransferRefusal::Framing, "page length");
		if (lengths.size() >= limits.pages || length > limits.bytes - total)
			refuse(TransferRefusal::Limits, "transfer over the page or byte limit");
		lengths.push_back(std::uint32_t(length));
		total += length;
		at += length;
	}
	Receiver r{base, limits};
	r.places = transfer_layout(lengths);
	const std::size_t count = r.places.back().segment;
	if (count > limits.segments)
		refuse(TransferRefusal::Limits, "transfer over the segment limit");
	std::vector<std::size_t> sizes(count);
	for (std::size_t i = 0; i < lengths.size(); ++i)
		sizes[r.places[i].segment - 1] = r.places[i].offset + lengths[i];
	std::vector<std::shared_ptr<distribution::OwnedBuffer>> segments;
	std::vector<SegmentId> ids;
	for (std::size_t n = 0; n < count; ++n) {
		ids.push_back(segment_id(std::uint32_t(n + 1)));
		if (zero(ids.back()))
			throw std::logic_error("zero transfer segment id");
		segments.push_back(distribution::make_owned_buffer(account, sizes[n]));
		segments.back()->resize(sizes[n]);
	}
	for (std::size_t i = 0, at = 0; i < lengths.size(); at += lengths[i++]) {
		const auto segment = segments[r.places[i].segment - 1]->mutable_bytes();
		std::copy_n(stream.begin() + at, lengths[i], segment.begin() + r.places[i].offset);
		r.pages.push_back(segment.subspan(r.places[i].offset, lengths[i]));
	}
	const auto n = r.pages.size();
	try {
		// D4.2: every header frames its page.
		for (std::size_t i = 0; i < n; ++i) {
			r.headers.push_back(p::decode_page_header(r.pages[i], limits));
			r.first.push_back(r.resolved.size());
			r.resolved.resize(r.resolved.size() + r.headers.back().externals);
		}
		// D4.3: walk from the root, resolving every child, and compare the seal order it implies.
		r.next_external.assign(n, 0);
		r.used.assign(n, false);
		r.visit(n - 1, {}, true);
		r.order.push_back(n - 1);
		if (std::count(r.used.begin(), r.used.end(), true) != std::ptrdiff_t(n - 1))
			refuse(TransferRefusal::Unreferenced, "a page no parent references");
		for (std::size_t i = 0; i < n; ++i)
			if (r.order[i] != i)
				refuse(TransferRefusal::Order, "transfer is not in seal order");
		// D4.4: bottom-up in transfer order, filling digests and placement, then dense-pages' checks.
		std::vector<p::Reference> references(n);
		for (std::size_t i = 0; i < n; ++i) {
			const auto page = r.pages[i];
			const auto &header = r.headers[i];
			for (std::size_t j = 0; j < header.externals; ++j) {
				const auto &resolved = r.resolved[r.first[i] + j];
				const auto &child = resolved.child == SIZE_MAX ? resolved.base : references[resolved.child];
				const auto content = content_at(page.size(), header.externals, j);
				const auto placement = placement_at(page.size(), header.externals, j);
				std::copy(child.segment.begin(), child.segment.end(), page.begin() + placement);
				write_integer(page, placement + 16, child.offset, 4);
				const auto stated = page.subspan(content, 32);
				if (!zero(stated) && !std::equal(stated.begin(), stated.end(), child.digest.begin()))
					refuse(TransferRefusal::ContentDigest, "content digest mismatch");
				if (read_integer(page, content + 32, 8) != child.keys ||
					read_integer(page, content + 40, 4) != child.length ||
					static_cast<unsigned char>(page[content + 44]) != child.height)
					refuse(TransferRefusal::ContentSummary, "content summary mismatch");
				std::copy(child.digest.begin(), child.digest.end(), stated.begin());
			}
			const auto digest = p::page_commitment(page, limits);
			const auto stated = page.subspan(32, 32);
			if (!zero(stated) && !std::equal(stated.begin(), stated.end(), digest.begin()))
				refuse(TransferRefusal::PageDigest, "page digest mismatch");
			std::copy(digest.begin(), digest.end(), stated.begin());
			auto &reference = references[i];
			reference.segment = ids[r.places[i].segment - 1];
			reference.offset = r.places[i].offset;
			reference.length = header.length;
			reference.keys = header.keys;
			reference.height = header.height;
			reference.digest = digest;
			PageValidator validator(account, segments[r.places[i].segment - 1], page, reference, limits, false);
			while (!validator.done()) {
				ValidationBudget budget{std::size_t(1) << 20, std::size_t(1) << 16};
				validator.step(budget);
			}
			check_leaves(page, header, target, limits);
		}
		// D4.5: the root.
		if (references.back().digest != target.root)
			refuse(TransferRefusal::RootDigest, "root digest mismatch");
		out.tree = references.back();
	} catch (const TransferRefused &) {
		throw;
	} catch (const std::invalid_argument &error) {
		refuse(TransferRefusal::Page, error.what());
	}
	for (std::size_t s = 0; s < count; ++s) {
		segments[s]->seal();
		SegmentOwnership ownership;
		ownership.segment = ids[s];
		ownership.length = sizes[s];
		ownership.checksum = p::pack_commitment(segments[s]->view());
		out.segments.push_back({ownership, std::move(segments[s])});
	}
	out.pages = n;
	out.bytes = total;
	out.base_children = r.base_children;
	out.base_pages_walked = r.base_walked;
	return out;
}
} // namespace detent::local::detail
