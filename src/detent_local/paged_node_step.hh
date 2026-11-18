#pragma once
#include "detent_local/paged_layout.hh"
#include <stdexcept>

namespace detent::local::paged {
// The one bounded forward step of every in-page walk (docs/mechanisms/dense-pages.md, "In-page walks").
class NodeAt {
  public:
	constexpr NodeAt() noexcept = default;
	static NodeAt after(const NodeView &node) noexcept { return NodeAt(node.end); }
	static NodeAt child(const NodeView &parent, std::uint16_t slot) {
		if (slot & 0x8000 || slot < parent.end)
			throw std::invalid_argument("inline slot does not progress");
		return NodeAt(slot);
	}
	std::size_t offset() const noexcept { return offset_; }
	bool first() const noexcept { return offset_ == page_header_bytes; }
	NodeView decode(std::span<const char> page, Limits limits) const {
		return decode_node(page, offset_, limits);
	}
	bool operator==(const NodeAt &) const = default;

  private:
	explicit constexpr NodeAt(std::size_t offset) noexcept : offset_(offset) {}
	std::uint32_t offset_ = page_header_bytes;
};
} // namespace detent::local::paged
