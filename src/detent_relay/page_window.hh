#pragma once
#include "detent_local/paged_layout.hh"
#include "paged_transfer.hh"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// Dense distribution D7.3 (docs/design/dense-distribution.md): the relay's page window and the
// top-down Merkle diff that names a delta transfer's pages.
namespace detent::relay {
// The pages of a target tree that a base tree lacks under dense-pages D7's identity (the same
// entering trie path, with equal digest, length, keys and height), in the target's seal order.
struct MerkleDiff {
	std::vector<local::paged::Reference> pages;
	// Page bytes without padding, and base pages read while descending.
	std::uint64_t bytes = 0, base_pages = 0;
};
// Descends from both roots in step, prunes every child whose summary equals the base's child at
// the same entering path, and emits what it keeps in seal order: a page longer than a page's
// capacity seals at its own top node, after its subtree; any other seals at its parent's node,
// after that node's subtree, lower first, then larger, then in label order (dense-pages D5).
// Reference{} is the empty tree; against an empty base the diff is the full copy in seal order.
// Page bytes stay valid for the call.
MerkleDiff merkle_diff(const local::paged::Reference &target, const local::detail::TransferPages &target_pages,
					   const local::paged::Reference &base, const local::detail::TransferPages &base_pages,
					   local::paged::Limits = {});

// One generation the window holds: its position, root page and page access, the owner that keeps
// those pages readable, and the segments they live in (id and length). Generations built from one
// another share every segment they both reach.
struct WindowGeneration {
	std::uint64_t position = 0;
	local::paged::Reference root;
	local::detail::TransferPages pages;
	std::shared_ptr<const void> owner;
	std::vector<std::pair<local::SegmentId, std::uint64_t>> segments;
};
// D7.3: the relay's generations at recent positions, which share every unchanged page. It is
// bounded like the record ring, by `positions` generations and by the head's own page bytes of
// retained segments (those the head does not reach); the oldest go first. A base outside it gets
// records (D6) or the full copy (D10).
class PageWindow {
  public:
	explicit PageWindow(std::size_t positions = 64, local::paged::Limits = {});
	// Makes `head` the newest generation, at a position above the current head's; `head_bytes` is
	// its page bytes (Root::bytes).
	void advance(WindowGeneration head, std::uint64_t head_bytes);
	// Replaces the head with the same state in other segments (its canonical relocation, D7.4).
	void rebase(WindowGeneration head);
	// Drops every generation: a new incarnation, or a head that is not the window's successor.
	void clear() noexcept;
	const WindowGeneration *head() const noexcept;
	const WindowGeneration *at(std::uint64_t position) const noexcept;
	// The delta transfer's pages from the generation at `base` to the head, or nothing when
	// `base` is outside the window.
	std::optional<MerkleDiff> delta(std::uint64_t base) const;
	std::size_t size() const noexcept { return generations_.size(); }
	// Bytes of the segments the window holds beyond the head's, and of all it holds.
	std::uint64_t retained_bytes() const noexcept { return held_ - head_segments_; }
	std::uint64_t held_bytes() const noexcept { return held_; }

  private:
	void hold(const WindowGeneration &, int change);
	std::uint64_t own(const WindowGeneration &) const;
	void trim();
	std::size_t positions_;
	local::paged::Limits limits_;
	std::deque<WindowGeneration> generations_; // oldest first
	// Per segment: its length and how many generations reach it.
	std::map<local::SegmentId, std::pair<std::uint64_t, std::size_t>> segments_;
	std::uint64_t held_ = 0, head_segments_ = 0, head_bytes_ = 0;
};
} // namespace detent::relay
