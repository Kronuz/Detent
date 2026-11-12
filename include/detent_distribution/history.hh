#pragma once
#include "detent/cursor.hh"
#include "detent/model.hh"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

// A namespace's history as a server answers from it (docs/design/namespace-delivery.md D6, W6;
// docs/status/namespace-s6.md). S5.2 implements NamespaceHistory; the session
// endpoint selects an answer from it and encodes the frames (session_frames.hh).
namespace detent::distribution {
// One key's write: its definition, or a tombstone, at the namespace position of the write.
struct HistoryRecord {
	std::string_view key;
	std::uint64_t revision = 0;
	const KnobDef *value = nullptr; // null is a tombstone
};
// Records in the reader's order. Each record, and what it points at, lives until the next call;
// null ends the sequence.
class HistoryReader {
  public:
	virtual ~HistoryReader() = default;
	virtual const HistoryRecord *next() = 0;
};
// Positions [first, last] compacted: each key written in them once, at its last write, in key
// order, tombstones kept. A named span is a persisted block, immutable and fetchable by
// (namespace, incarnation, first, last); an unnamed one is a level-0 section, and the unnamed
// spans, which are always the newest, are merged into one piece at answer time.
struct HistorySpan {
	std::uint64_t first = 0, last = 0;
	bool named = false;
	bool operator==(const HistorySpan &) const = default;
};
// One namespace's history, frozen for one answer:
//
//   base: state at cut() | spans tiling (cut, ring_start] | ring entries (ring_start, head]
//
class NamespaceHistory {
  public:
	virtual ~NamespaceHistory() = default;
	// The namespace's cluster, incarnation and newest position h.
	virtual NamespaceCursor head() const noexcept = 0;
	// p: a store below it gets a full copy.
	virtual std::uint64_t cut() const noexcept = 0;
	// s: the ring holds every position after it.
	virtual std::uint64_t ring_start() const noexcept = 0;
	// Oldest first, contiguous, from cut() + 1 to ring_start(); empty when they are equal.
	virtual std::span<const HistorySpan> spans() const noexcept = 0;
	virtual std::unique_ptr<HistoryReader> read_span(std::size_t index) const = 0;
	// The ring's records after `after` (at least ring_start()): by position, then key, each
	// key once per position, every position up to head() present.
	virtual std::unique_ptr<HistoryReader> read_ring(std::uint64_t after) const = 0;
	// The state at ring_start(): live records only, in key order.
	virtual std::unique_ptr<HistoryReader> read_full_copy() const = 0;
};
// A history that breaks its own contract: a server source failure, never a client's fault.
class HistoryFailure : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
// What a store at a cursor gets (D6's tiers):
//   Current   the store is at head; nothing to send
//   Ahead     the store is above head: no answer until this source catches up (D1)
//   Entries   R >= s: the ring's entries (R, h]
//   Blocks    p <= R < s: the span holding R + 1 trimmed above R, the younger spans, the ring
//   FullCopy  R < p, another cluster or incarnation, or no store: the state at s, the ring
enum class AnswerKind : std::uint8_t { Current = 0, Ahead = 1, Entries = 2, Blocks = 3, FullCopy = 4 };
struct Answer {
	AnswerKind kind = AnswerKind::Current;
	std::uint64_t after = 0; // R; 0 for a full copy
	std::size_t span = 0;	 // Blocks: the span holding R + 1
	bool operator==(const Answer &) const = default;
};
// Checks the history's shape (HistoryFailure) and picks the tier for `store`.
Answer select_answer(const NamespaceHistory &, const std::optional<NamespaceCursor> &store);
} // namespace detent::distribution
