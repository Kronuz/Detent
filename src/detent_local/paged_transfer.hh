#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_local/ownership.hh"
#include "detent_local/paged_layout.hh"
#include "paged_snapshot_builder.hh"
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

// Dense distribution D2-D4 (docs/design/dense-distribution.md; docs/mechanisms/paged-transfer.md).
namespace detent::local::detail {
// A generation's page by its reference; the bytes stay valid for the call that reads them.
using TransferPages = std::function<std::span<const char>(const paged::Reference &)>;

// (segment number from 1, offset) of a page in a transfer's derived layout.
struct TransferPlace {
	std::uint32_t segment = 0, offset = 0;
	bool operator==(const TransferPlace &) const = default;
};
// D2: the build's layout of pages of these lengths: each at the next 8-byte offset, in a new
// segment of at most 1 MiB when it does not fit.
std::vector<TransferPlace> transfer_layout(std::span<const std::uint32_t> lengths);

// D2, D3: the wire form of `pages`, the target's pages the receiver lacks, in the target's seal
// order. Every header digest is zero; a child in `pages` has a zero content digest and its place
// in the derived layout as placement (12 zero bytes, segment number, offset); any other child keeps
// its digest and has a zero placement. Throws std::logic_error when a child follows its parent.
std::vector<char> make_transfer(std::span<const paged::Reference> pages, const TransferPages &,
								paged::Limits = {});

// The receiver's base generation: its root page (Reference{} for the empty state) and pages it
// already verified. Base children are found by trie path from this root (D5).
struct TransferBase {
	paged::Reference root;
	TransferPages pages;
};

// Why a transfer is refused (D4's checks, in the order the receiver runs them).
enum class TransferRefusal {
	Framing,		// a page without magic or with a length outside [79, 65536] or past the stream
	Limits,			// more pages, bytes or segments than the limits admit
	Empty,			// an empty transfer naming neither the empty state nor the base's root
	Layout,			// a placement names no page start of the derived layout
	ParentFirst,	// a child at or after its parent
	Twice,			// a page referenced twice
	BaseLacks,		// no base page at a zero placement's trie path
	Unreferenced,	// a page but the last that no parent references
	Order,			// the transfer is not in the seal order its pages imply
	ContentDigest,	// a stated content digest differs from the child's
	ContentSummary, // a content entry's keys, length or height differ from the child's
	PageDigest,		// a stated header digest differs from the page's
	Page,			// dense-pages' page checks: structure, labels, keys, height, D5 packing
	Leaf,			// a leaf body disagrees with the frame's position (dense-pages D4.4)
	RootDigest,		// the last page's digest is not the target's
};
class TransferRefused : public std::invalid_argument {
  public:
	TransferRefused(TransferRefusal reason, const char *what) : std::invalid_argument(what), reason_(reason) {}
	TransferRefusal reason() const noexcept { return reason_; }

  private:
	TransferRefusal reason_;
};

// A segment the receiver writes: its bytes (padding included) and ownership with the pack checksum.
struct ReceivedSegment {
	SegmentOwnership ownership;
	std::shared_ptr<const distribution::OwnedBuffer> bytes;
};
struct ReceivedTransfer {
	// Numbered from 1 in order; each id is segment_id(number).
	std::vector<ReceivedSegment> segments;
	// The target's root page: in `segments`, the base's root for an empty transfer over a base at
	// the target, or Reference{} for the empty state.
	paged::Reference tree;
	// Pages written and their bytes without padding; base children resolved, base pages walked;
	// namespace leaves in the pages written (a full copy's every namespace).
	std::uint64_t pages = 0, bytes = 0, base_children = 0, base_pages_walked = 0, namespaces = 0;
};
using TransferSegmentId = std::function<SegmentId(std::uint32_t number)>;

// What the frame names: the target's root digest and its position, whose revision bounds every leaf.
struct TransferTarget {
	paged::Digest root{};
	std::uint64_t revision = 0;
	std::uint32_t maximum_overrides = 256;
};
// D4: verifies `stream`, a transfer over `base` (null for the empty base), against `target`, and
// returns the segments to write. It parses and walks every page, reruns D5 through dense-pages'
// page validator, checks each new leaf as a first touch does, hashes each page once bottom-up and
// each segment once for its pack checksum, and builds nothing. Throws TransferRefused.
ReceivedTransfer receive_transfer(std::shared_ptr<distribution::StorageLedger>, std::span<const char> stream,
								  const TransferBase *base, const TransferTarget &target,
								  const TransferSegmentId &segment_id, paged::Limits);
// DD3: a full copy received as generation `name` at `target`. D4 over the empty base, with the
// generation's derived segment ids from ordinal 0, so the image is the one a build of the same state
// writes, and no packer runs. Throws TransferRefused.
PagedImage receive_image(std::shared_ptr<distribution::StorageLedger>, std::span<const char> stream,
						 const paged::Digest &root, const distribution::NamespaceCursor &target,
						 const distribution::Profile &, const DerivedGeneration &name, paged::Limits,
						 std::uint32_t maximum_overrides);
} // namespace detent::local::detail
