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
// What a store builds from a response's frames before any of them is visible: one prepared frame
// per namespace (in the agent, its generation). The frames of a response stay prepared, admitted
// by the client's ledger as they grow, until the whole response has been verified; then every one
// is published together. A failed response prepares nothing that anyone can see.
class FramePreparer {
  public:
	class Prepared {
	  public:
		virtual ~Prepared() = default;
		virtual void piece(distribution::PieceKind, std::uint64_t first, std::uint64_t last) = 0;
		virtual void record(std::string_view key, std::uint64_t revision, const KnobDef *value) = 0;
		// The memory it holds: admitted to the ledger as it grows.
		virtual std::size_t bytes() const noexcept = 0;
		// The response is complete: make it visible.
		virtual void publish() = 0;
	};
	virtual ~FramePreparer() = default;
	virtual std::unique_ptr<Prepared> prepare(std::string_view name, distribution::FrameKind, std::uint64_t after,
											  const distribution::NamespaceCursor &to) = 0;
	// The response's directory changes, once, before its frames are published.
	virtual void directory(const distribution::DirectoryUpdate &) {}
};
// Decodes one session response: HTTP/1.1, then for next's 200 the fragments (UpdateKind::Frames)
// and the frames into `sink`; 204 is idle; another status is a reset (SessionResetError). The
// decoder's own memory is admitted by `limits.storage_account`.
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
	distribution::StorageOwner<Impl> impl_;
};
task::task<NamespaceSession> open_namespace_session(ClientOptions, Following, task::cancel_token = {});
// Prepares the response's frames and publishes them only when next returns Frames. Throws
// distribution::StoragePressure when `options.limits.storage_account` cannot hold them.
task::task<FramesOutcome> next_namespace_session(ClientOptions, NamespaceSession, Following,
												 std::chrono::milliseconds wait, FramePreparer &,
												 task::cancel_token = {});
task::task<void> close_namespace_session(ClientOptions, NamespaceSession, task::cancel_token = {});
} // namespace detent::delivery
