#pragma once
#include "detent_distribution/session_frames.hh"
#include "page_window.hh"
#include "paged_graph_validator.hh"
#include "paged_snapshot_builder.hh"
#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

// Dense distribution D7 on the relay (docs/design/dense-distribution.md): the generations a relay
// builds from the records it mirrors, and the canonical full copy in wire form (D2, D3).
namespace detent::relay {
// One generation of a namespace at a cursor, built from records: a full build (D7.2's cold start),
// a build over the previous generation (dense-pages D7), or a generation relocated into seal order
// (D7.4). A build over a base shares the base's segments that its tree still reaches.
class Generation {
  public:
	~Generation();
	Generation(const Generation &) = delete;
	Generation &operator=(const Generation &) = delete;
	const distribution::NamespaceCursor &at() const noexcept { return root_.cursor; }
	// The root page (Reference{} for the empty state) and the state commitment a frame names.
	const local::paged::Reference &tree() const noexcept { return root_.tree; }
	const distribution::RootDigest &root() const noexcept { return commitment_; }
	// Bytes of the pages its tree reaches, and of the segments they live in.
	std::uint64_t page_bytes() const noexcept { return root_.bytes; }
	std::uint64_t physical_bytes() const noexcept;
	// The segments its pages live in, by id.
	std::span<const local::detail::BuiltSegment> segments() const noexcept { return segments_; }
	// The generation's pages by reference, valid while this generation lives.
	local::detail::TransferPages pages() const;
	local::paged::Limits limits() const noexcept;

  private:
	Generation() = default;
	void hold(std::vector<std::pair<local::detail::BuiltSegment, local::detail::SegmentReachability>>);
	local::paged::Root root_;
	local::Descriptor descriptor_;
	distribution::RootDigest commitment_{};
	std::vector<local::detail::BuiltSegment> segments_;
	std::vector<local::detail::SegmentInput> inputs_;
	std::vector<local::detail::SegmentReachability> live_;
	friend std::shared_ptr<const Generation> build_generation(std::string_view, std::shared_ptr<const Generation>,
															  const std::atomic<bool> *);
	friend std::shared_ptr<const Generation> relocate(const Generation &, const std::atomic<bool> *);
};
// Builds the namespace that `body`, a W6 body holding one records frame, carries: a full copy when
// `base` is null, else entries or blocks after the base's position. Throws std::invalid_argument on
// any other body, and std::runtime_error once `cancel` is set.
std::shared_ptr<const Generation> build_generation(std::string_view body, std::shared_ptr<const Generation> base = {},
												   const std::atomic<bool> *cancel = nullptr);
// D7.4: `generation` relocated into seal order (DD2), in the files a full build at its cursor writes.
std::shared_ptr<const Generation> relocate(const Generation &, const std::atomic<bool> *cancel = nullptr);

// D7.4: the full copy at a position: the transfer from the empty base, every page in seal order.
struct CanonicalCopy {
	distribution::NamespaceCursor at;
	local::paged::Reference tree;
	distribution::RootDigest root{};
	std::vector<char> transfer;
};
// The full copy of `canonical`, a generation whose files are in seal order (a full build or a
// relocation).
std::shared_ptr<const CanonicalCopy> full_copy(const Generation &canonical);
// The window's entry for `generation`, which it keeps alive.
WindowGeneration window_entry(std::shared_ptr<const Generation>);
// The delta transfer of `diff`'s pages, read from `target`.
std::vector<char> delta_transfer(const MerkleDiff &diff, const Generation &target);
} // namespace detent::relay
