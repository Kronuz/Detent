#pragma once
// One session answer (W5/W6) on the owner: the frames encoded in bounded steps into chunks the
// delivery ledger admits, then sent as DTDIST01 fragments of UpdateKind::Frames once the body's
// length is known. docs/status/namespace-s6-cutover.md.
#include "detent_distribution/framing.hh"
#include "detent_distribution/grants.hh"
#include "detent_distribution/session_frames.hh"
#include "namespace_history.hh"
#include <deque>

namespace detent::authority {
struct FramesRequest {
	std::string name;
	std::unique_ptr<AuthorityHistory> history; // null for a dropped namespace
	std::optional<distribution::NamespaceCursor> store;
	std::optional<distribution::NamespaceCursor> dropped; // the dropped incarnation's cursor
};
// A live namespace the voter cannot freeze yet (its layout is not at the sealed state): no answer
// is better than one that omits it, so the session's next fails and the client keeps its cursors.
class FramesBehind : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
struct FramesPlan {
	std::vector<FramesRequest> requests; // in name order
	std::optional<distribution::DirectoryUpdate> directory;
};
using Followed = std::pair<std::string, std::optional<distribution::NamespaceCursor>>;
// What a next answers: a frame for each followed namespace (a drop for one the session stores and
// the voter has dropped), and with `all` the directory's changes the grants allow and a full copy of
// each live granted namespace they bring. Throws FramesBehind, and HistoryFailure for a history
// that breaks its contract.
FramesPlan plan_frames(std::span<const Followed> follows, const std::optional<distribution::DirectoryCursor> &all,
					   const distribution::NamespaceGrants &grants, const State &state, const Identity &cluster,
					   const State &sealed, const history::Table &table, history::Files &files,
					   const ChangeHistory &ring);
class FramesExport {
  public:
	// `requests` in strictly increasing name order. Throws HistoryFailure for a history that
	// breaks its contract.
	FramesExport(std::vector<FramesRequest> requests, std::optional<distribution::DirectoryUpdate> directory,
				 distribution::Identity update, bool compress, std::shared_ptr<distribution::StorageLedger> account,
				 std::size_t maximum_bytes);
	~FramesExport();
	FramesExport(const FramesExport &) = delete;
	FramesExport &operator=(const FramesExport &) = delete;
	// True when no namespace has a frame and the directory has no change: the session waits.
	bool empty() const noexcept { return empty_; }
	// Encodes, then frames; throws std::length_error past `maximum_bytes`.
	void step(distribution::WorkBudget &);
	// The body is encoded: its length is known and fragments follow.
	bool counted() const noexcept { return counted_; }
	std::uint64_t encoded_length() const noexcept { return total_; }
	bool frame_ready() const noexcept;
	std::shared_ptr<const distribution::OwnedBuffer> take_frame();
	bool done() const noexcept;

  private:
	void seal_chunk();
	std::vector<FramesRequest> owned_;
	std::vector<distribution::NamespaceRequest> requests_;
	std::optional<distribution::SessionFramesEncoder> encoder_;
	std::shared_ptr<distribution::StorageLedger> account_;
	std::size_t maximum_;
	std::string scratch_;
	std::shared_ptr<distribution::OwnedBuffer> chunk_;
	std::deque<std::shared_ptr<const distribution::OwnedBuffer>> chunks_;
	std::optional<distribution::OwnedFragmentEncoder> fragments_;
	distribution::Identity update_;
	bool compress_, counted_ = false, empty_ = false, active_ = false, done_ = false;
	std::uint64_t total_ = 0, offered_ = 0;
};
} // namespace detent::authority
