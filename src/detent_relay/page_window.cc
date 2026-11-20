#include "page_window.hh"
#include <algorithm>
#include <stdexcept>

namespace detent::relay {
namespace p = local::paged;
using local::detail::TransferPages;
namespace {
bool same(const p::Reference &a, const p::Reference &b) noexcept {
	return a.length == b.length && a.keys == b.keys && a.height == b.height && a.digest == b.digest;
}
// A node of the base tree: its page's bytes and offset; no page when the base has no node there.
struct Place {
	std::span<const char> page;
	std::size_t offset = 0;
};
class Descent {
  public:
	Descent(const TransferPages &target, const TransferPages &base, p::Limits limits)
		: target_(target), base_(base), limits_(limits) {}
	Place base_page(const p::Reference &reference) {
		++out.base_pages;
		return {checked(base_(reference), reference), p::page_header_bytes};
	}
	void page(const p::Reference &reference, Place base) {
		node(checked(target_(reference), reference), p::page_header_bytes, base);
	}
	void emit(const p::Reference &reference) {
		out.pages.push_back(reference);
		out.bytes += reference.length;
	}
	MerkleDiff out;

  private:
	static std::span<const char> checked(std::span<const char> bytes, const p::Reference &reference) {
		if (bytes.size() != reference.length)
			throw std::invalid_argument("page window: a page's bytes differ from its reference");
		return bytes;
	}
	void node(std::span<const char> bytes, std::size_t offset, Place base) {
		const auto n = p::decode_node(bytes, offset, limits_);
		if (n.kind != p::PageKind::Branch)
			return;
		// The base node at this entering path is this node's partner only with the same prefix.
		std::optional<p::NodeView> m;
		if (!base.page.empty()) {
			auto view = p::decode_node(base.page, base.offset, limits_);
			if (view.kind == p::PageKind::Branch && std::ranges::equal(view.prefix, n.prefix))
				m = view;
		}
		const auto first = chosen_.size();
		std::size_t j = 0;
		for (std::size_t i = 0; i < n.labels.size(); ++i) {
			const auto label = static_cast<unsigned char>(n.labels[i]);
			Place child_base;
			std::optional<p::Reference> base_child;
			if (m) {
				while (j < m->labels.size() && static_cast<unsigned char>(m->labels[j]) < label)
					++j;
				if (j < m->labels.size() && static_cast<unsigned char>(m->labels[j]) == label) {
					const auto slot = m->slot(j);
					if (slot & 0x8000)
						base_child = p::decode_external(base.page, slot & 0x7fff, limits_);
					else
						child_base = {base.page, slot};
				}
			}
			const auto slot = n.slot(i);
			if (!(slot & 0x8000)) {
				node(bytes, slot, base_child ? base_page(*base_child) : child_base);
				continue;
			}
			const auto child = p::decode_external(bytes, slot & 0x7fff, limits_);
			if (base_child && same(child, *base_child))
				continue;
			page(child, base_child ? base_page(*base_child) : child_base);
			if (child.length > p::page_capacity)
				emit(child); // sealed at its own top node, after its subtree
			else
				chosen_.push_back(child);
		}
		std::stable_sort(chosen_.begin() + first, chosen_.end(), [](const auto &a, const auto &b) {
			return p::seals_before(a.height, a.length, b.height, b.length);
		});
		for (auto at = first; at < chosen_.size(); ++at)
			emit(chosen_[at]);
		chosen_.resize(first);
	}
	const TransferPages &target_, &base_;
	p::Limits limits_;
	std::vector<p::Reference> chosen_;
};
} // namespace

MerkleDiff merkle_diff(const p::Reference &target, const TransferPages &target_pages, const p::Reference &base,
					   const TransferPages &base_pages, p::Limits limits) {
	if (target == p::Reference{} || (base != p::Reference{} && same(target, base)))
		return {};
	Descent descent(target_pages, base_pages, limits);
	descent.page(target, base == p::Reference{} ? Place{} : descent.base_page(base));
	descent.emit(target);
	return std::move(descent.out);
}

PageWindow::PageWindow(std::size_t positions, p::Limits limits) : positions_(positions), limits_(limits) {
	if (!positions_)
		throw std::invalid_argument("page window: no positions");
}
void PageWindow::advance(WindowGeneration head, std::uint64_t head_bytes) {
	head.retained = 0;
	if (!generations_.empty()) {
		auto &previous = generations_.back();
		if (head.position <= previous.position)
			throw std::invalid_argument("page window: a head at or below the window's");
		previous.retained =
			merkle_diff(previous.root, previous.pages, head.root, head.pages, limits_).bytes;
		retained_ += previous.retained;
	}
	generations_.push_back(std::move(head));
	head_bytes_ = head_bytes;
	while (generations_.size() > positions_ || retained_ > head_bytes_) {
		retained_ -= generations_.front().retained;
		generations_.pop_front();
	}
}
void PageWindow::clear() noexcept {
	generations_.clear();
	retained_ = head_bytes_ = 0;
}
const WindowGeneration *PageWindow::head() const noexcept {
	return generations_.empty() ? nullptr : &generations_.back();
}
const WindowGeneration *PageWindow::at(std::uint64_t position) const noexcept {
	const auto found = std::ranges::lower_bound(generations_, position, {}, &WindowGeneration::position);
	return found == generations_.end() || found->position != position ? nullptr : &*found;
}
std::optional<MerkleDiff> PageWindow::delta(std::uint64_t base) const {
	const auto *from = at(base);
	if (!from)
		return std::nullopt;
	const auto &to = generations_.back();
	return merkle_diff(to.root, to.pages, from->root, from->pages, limits_);
}
} // namespace detent::relay
