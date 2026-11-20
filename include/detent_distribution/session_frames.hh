#pragma once
#include "allocator.hh"
#include "history.hh"
#include "protocol.hh"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

// W6: one session response carries every namespace's frame (docs/status/namespace-s6.md has the
// grammar). The body is a sequence of MessagePack values:
//
//   [1, directory | nil, frame count]
//   per frame:  [name, frame kind, after, [cluster, incarnation, position], root digest | nil, piece count]
//               the root digest is the target's state commitment when the sender built it (dense
//               distribution D1), nil otherwise
//               a dropped frame: [name, 4, 0, the dropped incarnation's cursor, 0]
//   per piece:  [piece kind, first, last]  then records  then nil
//   record:     [key, revision, definition | nil]          nil definition: a tombstone
//   directory:  [cluster, from, to, [[name, incarnation, live], ...]]
//   a pages frame (kind 5): [name, 5, after, cursor, root digest, offset, chunk count], then the
//               transfer's bytes from `offset`, a whole chunk's, as that many bin values of 1 to 64 KiB
//               (dense distribution D2, D8: a fetch resumes from its offset)
//
// Frames come in name order. A piece's records come in key order, each key once.
namespace detent::distribution {
// Dropped: the namespace was dropped; the cursor is its last incarnation's, at the drop. It is sent
// to a session that follows the namespace with a store of it and no longer has a live namespace.
// Pages: a transfer to the cursor over the receiver's generation at `after` (0: the empty state),
// committed to by the target's root digest (dense distribution D1-D4).
enum class FrameKind : std::uint8_t { Entries = 1, Blocks = 2, FullCopy = 3, Dropped = 4, Pages = 5 };
// Entry: one ring position [p, p]. Block: a named span sent whole. Span: a trimmed or merged
// span, not a stored object. FullCopy: the state at `last`, first 0, no tombstones.
enum class PieceKind : std::uint8_t { Entry = 1, Block = 2, Span = 3, FullCopy = 4 };
inline constexpr std::uint64_t session_frames_version = 1;
// D5: directory changes after `from`, up to `to`: a creation, re-creation (new incarnation) or drop.
struct DirectoryEntry {
	std::string name;
	Identity incarnation{};
	bool live = true;
	bool operator==(const DirectoryEntry &) const = default;
};
struct DirectoryUpdate {
	DirectoryCursor to;
	std::uint64_t from = 0;
	std::vector<DirectoryEntry> entries;
	bool operator==(const DirectoryUpdate &) const = default;
};
using RootDigest = std::array<char, 32>;
inline constexpr std::size_t transfer_chunk_bytes = 64 * 1024;
// A transfer a sender answers with in place of records; its bytes are borrowed until the encoder ends.
// The frame carries them from `offset`, a multiple of transfer_chunk_bytes at most their size (D8).
struct PagesAnswer {
	RootDigest root{};
	std::uint64_t after = 0;
	NamespaceCursor to;
	std::span<const char> bytes;
	std::uint64_t offset = 0;
};
// A requested namespace: its history (borrowed until the encoder ends) and the store's cursor, or,
// for a namespace dropped for good, the cursor of its dropped incarnation and no history.
struct NamespaceRequest {
	std::string_view name;
	const NamespaceHistory *history = nullptr;
	std::optional<NamespaceCursor> store;
	std::optional<NamespaceCursor> dropped;
	const PagesAnswer *pages = nullptr; // a pages frame instead of the history's answer
	// A records answer's root digest: the history head's state commitment when the sender built it
	// (D1), else zero, which the frame sends as nil.
	RootDigest root{};
	bool repair = false; // `store` is the floor of a full copy (D139; select_answer)
};
// Selects every namespace's answer at construction, then emits the body in bounded steps. A
// Current or Ahead answer gets no frame. Names must be valid and strictly increasing. Its own
// memory is admitted by `account` (a private ledger when none): construction throws
// StoragePressure when the plan does not fit, and a piece's readers are admitted as it opens.
class SessionFramesEncoder {
  public:
	SessionFramesEncoder(std::optional<DirectoryUpdate>, std::span<const NamespaceRequest>,
						 std::shared_ptr<StorageLedger> account = {});
	~SessionFramesEncoder();
	SessionFramesEncoder(const SessionFramesEncoder &) = delete;
	SessionFramesEncoder &operator=(const SessionFramesEncoder &) = delete;
	// Appends whole values until `out` holds at least `bytes` or the body ends; true at the end.
	bool step(std::string &out, std::size_t bytes);
	// Each request's answer, in request order.
	std::span<const Answer> answers() const noexcept;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
std::string encode_session_frames(std::optional<DirectoryUpdate>, std::span<const NamespaceRequest>,
								  std::shared_ptr<StorageLedger> account = {});

// Receives a validated body in order. A record's key and value live until the call returns.
class FrameSink {
  public:
	virtual ~FrameSink() = default;
	virtual void directory(const DirectoryUpdate &) {}
	virtual void frame(std::string_view name, FrameKind, std::uint64_t after, const NamespaceCursor &to) = 0;
	virtual void piece(PieceKind, std::uint64_t first, std::uint64_t last) = 0;
	virtual void record(std::string_view key, std::uint64_t revision, const KnobDef *value) = 0;
	// After frame(): the root digest the frame names, when it names one (always for pages).
	virtual void root(const RootDigest &) {}
	// A pages frame: the offset its chunks start at, then each chunk in order.
	virtual void pages(std::uint64_t) {}
	virtual void chunk(std::string_view) {}
	virtual void end_frame() {}
};
// Its memory (working strings and the directory) is admitted by `limits.storage_account`, which
// throws StoragePressure at its bound.
// Checks the grammar and every frame's positions (contiguous from after + 1 to the target, each
// record inside its piece and above `after`, keys of the frame's namespace in order, no
// tombstone in a full copy; a pages frame's chunks within `limits.bytes`) before the sink sees the
// value that completes them. A sink must
// still treat a frame as provisional until finish() returns.
class SessionFramesDecoder {
  public:
	explicit SessionFramesDecoder(FrameSink &, Limits = {});
	~SessionFramesDecoder();
	SessionFramesDecoder(const SessionFramesDecoder &) = delete;
	SessionFramesDecoder &operator=(const SessionFramesDecoder &) = delete;
	// Consumes at most `budget` bytes of `input`, advancing both.
	void feed(std::span<const char> &input, std::size_t &budget);
	// Requires the complete body and nothing after it.
	void finish();
	bool complete() const noexcept;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
