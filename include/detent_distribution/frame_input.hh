#pragma once
#include "detent/model.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/session_frames.hh"
#include "detent_distribution/storage.hh"
#include <span>
#include <vector>

// One namespace's W6 frame as the paged builders read it (namespace-delivery W6, D8 "Catch-up
// input"): a full copy, with the ring's entries after it, becomes a one-namespace ValidatedSnapshot
// at the frame's target; entries and blocks become a ValidatedDelta whose transactions group the
// frame's last record per key by revision. A tombstone for a key the base lacks stays a delete,
// which the builders apply as a no-op.
namespace detent::distribution {
// The v1 cursor the paged builders key a namespace generation by: the W1 cursor, the incarnation in
// the group's and the restore epoch's places, and a digest of the namespace's name in the scope's.
Cursor frame_cursor(std::string_view name, const NamespaceCursor &);

class FrameInput {
  public:
	// `limits.storage_account` admits every byte the input holds.
	FrameInput(std::string_view name, FrameKind, std::uint64_t after, const NamespaceCursor &to, Profile,
			   const Limits &);
	~FrameInput();
	FrameInput(FrameInput &&) noexcept = default;
	FrameInput &operator=(FrameInput &&) noexcept = default;
	void piece(PieceKind, std::uint64_t first, std::uint64_t last);
	// The decoder has checked the key, its order inside the piece and its revision.
	void record(std::string_view key, std::uint64_t revision, const KnobDef *value);
	std::size_t bytes() const noexcept;
	FrameKind kind() const noexcept { return kind_; }
	const std::string &name() const noexcept { return name_; }
	const NamespaceCursor &target() const noexcept { return to_; }
	std::uint64_t after() const noexcept { return after_; }
	// Once, after the response verified: a FullCopy frame's snapshot, or an Entries or Blocks
	// frame's delta from `after` under the target's incarnation.
	std::shared_ptr<ValidatedSnapshot> snapshot();
	std::shared_ptr<ValidatedDelta> delta();
	// A Pages frame: the root digest it commits to, then its transfer's chunks, admitted as they arrive.
	void pages(const RootDigest &);
	void chunk(std::string_view);
	const RootDigest &root() const noexcept { return root_; }
	std::span<const char> transfer() const noexcept { return transfer_; }

  private:
	struct Row {
		std::string_view key;
		std::uint64_t revision = 0;
		WireDefinition definition;
		bool tombstone = false;
	};
	template <class Storage> void add(Storage &, std::string_view key, std::uint64_t revision, const KnobDef *);
	// The last row of each key, in key order.
	std::vector<std::uint64_t> winners() const;
	std::string name_;
	FrameKind kind_;
	std::uint64_t after_;
	NamespaceCursor to_;
	Profile profile_;
	Limits limits_;
	std::shared_ptr<ValidatedSnapshot> snapshot_;
	std::shared_ptr<ValidatedDelta> delta_;
	std::unique_ptr<StableRows<Row>> rows_;
	bool ordered_ = true; // every row's key above the previous one's and no tombstone
	RootDigest root_{};
	std::vector<char> transfer_;
	StorageLedger::Ticket transfer_charge_;
};
} // namespace detent::distribution
