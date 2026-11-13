#pragma once
#include "client.hh"
#include "detent_distribution/session_frames.hh"

// The agent's side of W5/W6 (docs/status/namespace-s6.md): one session endpoint whose open and
// next state every followed namespace's cursor, or `all` with a directory cursor, and whose next
// answers with every namespace's frame in one response.
namespace detent::delivery {
struct FollowedNamespace {
	std::string name;
	std::optional<distribution::NamespaceCursor> cursor; // none: no store of it
};
struct Following {
	std::vector<FollowedNamespace> namespaces; // strictly increasing names
	std::optional<distribution::DirectoryCursor> all;
};
// A W5 body (version 2). The request's cluster is the directory cursor's, else the first store
// cursor's; a cursor of another cluster is sent as no store, which earns the same full copy.
std::string namespace_session_body(SessionResponsePurpose, const Following &, bool compression,
								   distribution::Identity session = {}, std::chrono::milliseconds wait = {});
struct NamespaceSession {
	distribution::Identity id{};
	std::chrono::milliseconds maximum_wait{0};
	bool valid() const noexcept { return id != distribution::Identity{}; }
};
enum class FramesOutcome { Frames, Idle };
// Decodes one session response: HTTP/1.1, then for next's 200 the fragments (UpdateKind::Frames)
// and the frames into `sink`; 204 is idle; another status is a reset (SessionResetError).
class FramesResponseDecoder {
  public:
	FramesResponseDecoder(SessionResponsePurpose, distribution::Identity session, bool compression,
						  distribution::FrameSink *, distribution::Limits = {});
	~FramesResponseDecoder();
	FramesResponseDecoder(const FramesResponseDecoder &) = delete;
	FramesResponseDecoder &operator=(const FramesResponseDecoder &) = delete;
	void feed(std::span<const char> bytes);
	// The connection closed: the response must be complete.
	void eof();
	bool done() const noexcept;
	bool reusable() const noexcept;
	// After done(): Open's session, or Next's outcome; a reset throws SessionResetError.
	NamespaceSession opened() const;
	FramesOutcome outcome() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// `sink` sees provisional frames: a frame counts once next returns Frames.
task::task<NamespaceSession> open_namespace_session(ClientOptions, Following, task::cancel_token = {});
task::task<FramesOutcome> next_namespace_session(ClientOptions, NamespaceSession, Following,
												 std::chrono::milliseconds wait, distribution::FrameSink &,
												 task::cancel_token = {});
task::task<void> close_namespace_session(ClientOptions, NamespaceSession, task::cancel_token = {});
} // namespace detent::delivery
