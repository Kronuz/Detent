#pragma once
#include "detent_distribution/session_frames.hh"
#include "page_window.hh"
#include "paged_snapshot_builder.hh"
#include <memory>
#include <string_view>
#include <vector>

// Dense distribution D7 on the relay (docs/design/dense-distribution.md): the generation a relay
// builds from the records it mirrors, and that generation's full copy in wire form (D2, D3).
namespace detent::relay {
// One full build of a namespace at a cursor. Its files are canonical (D7.4: a cold build is its own
// canonical copy), named as an agent names a build from a full copy: next_generation(none, at,
// Profile{}, true), segment n of the transfer at derive_segment(generation, n - 1).
class BuiltCopy {
  public:
	~BuiltCopy();
	BuiltCopy(const BuiltCopy &) = delete;
	BuiltCopy &operator=(const BuiltCopy &) = delete;
	const distribution::NamespaceCursor &at() const noexcept { return at_; }
	// The root page (Reference{} for the empty state) and the state commitment a frame names.
	const local::paged::Reference &tree() const noexcept { return tree_; }
	const distribution::RootDigest &root() const noexcept { return root_; }
	std::uint64_t page_bytes() const noexcept { return page_bytes_; }
	// The transfer from the empty base: every page in seal order, in wire form.
	std::span<const char> transfer() const noexcept { return transfer_; }
	// The built segments, in ordinal order.
	std::span<const local::detail::BuiltSegment> segments() const;
	// The generation's pages by reference, valid while this copy lives.
	local::detail::TransferPages pages() const;
	local::paged::Limits limits() const noexcept;

  private:
	struct Impl;
	BuiltCopy();
	std::unique_ptr<Impl> impl_;
	distribution::NamespaceCursor at_;
	local::paged::Reference tree_;
	distribution::RootDigest root_{};
	std::uint64_t page_bytes_ = 0;
	std::vector<char> transfer_;
	friend std::shared_ptr<const BuiltCopy> build_copy(std::string_view body);
};
// Builds the namespace that `body`, a W6 body holding one records full copy, carries: the state at
// its target. Throws std::invalid_argument on any other body.
std::shared_ptr<const BuiltCopy> build_copy(std::string_view body);
// The delta transfer of `diff`'s pages, read from `target`.
std::vector<char> delta_transfer(const MerkleDiff &diff, const BuiltCopy &target);
} // namespace detent::relay
