#pragma once
#include "history.hh"
#include "protocol.hh"
#include <memory>
#include <string>
#include <vector>

// W6: one session response carries every namespace's frame (docs/status/namespace-s6.md has the
// grammar). The body is a sequence of MessagePack values:
//
//   [1, directory | nil, frame count]
//   per frame:  [name, frame kind, after, [cluster, incarnation, position], piece count]
//   per piece:  [piece kind, first, last]  then records  then nil
//   record:     [key, revision, definition | nil]          nil definition: a tombstone
//   directory:  [cluster, from, to, [[name, incarnation, live], ...]]
//
// Frames come in name order. A piece's records come in key order, each key once.
namespace detent::distribution {
enum class FrameKind : std::uint8_t { Entries = 1, Blocks = 2, FullCopy = 3 };
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
// A requested namespace: its history (borrowed until the encoder ends) and the store's cursor.
struct NamespaceRequest {
	std::string_view name;
	const NamespaceHistory *history = nullptr;
	std::optional<NamespaceCursor> store;
};
// Selects every namespace's answer at construction, then emits the body in bounded steps. A
// Current or Ahead answer gets no frame. Names must be valid and strictly increasing.
class SessionFramesEncoder {
  public:
	SessionFramesEncoder(std::optional<DirectoryUpdate>, std::span<const NamespaceRequest>);
	~SessionFramesEncoder();
	SessionFramesEncoder(const SessionFramesEncoder &) = delete;
	SessionFramesEncoder &operator=(const SessionFramesEncoder &) = delete;
	// Appends whole values until `out` holds at least `bytes` or the body ends; true at the end.
	bool step(std::string &out, std::size_t bytes);
	// Each request's answer, in request order.
	std::span<const Answer> answers() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
std::string encode_session_frames(std::optional<DirectoryUpdate>, std::span<const NamespaceRequest>);

// Receives a validated body in order. A record's key and value live until the call returns.
class FrameSink {
  public:
	virtual ~FrameSink() = default;
	virtual void directory(const DirectoryUpdate &) {}
	virtual void frame(std::string_view name, FrameKind, std::uint64_t after, const NamespaceCursor &to) = 0;
	virtual void piece(PieceKind, std::uint64_t first, std::uint64_t last) = 0;
	virtual void record(std::string_view key, std::uint64_t revision, const KnobDef *value) = 0;
	virtual void end_frame() {}
};
// Checks the grammar and every frame's positions (contiguous from after + 1 to the target, each
// record inside its piece and above `after`, keys of the frame's namespace in order, no
// tombstone in a full copy) before the sink sees the value that completes them. A sink must
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
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::distribution
