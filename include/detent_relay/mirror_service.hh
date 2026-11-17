#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/grants.hh"
#include "detent_distribution/session_table.hh"
#include "detent_relay/namespace_relay.hh"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>

// The relay's namespace service (W8; docs/status/namespace-s8.md, "The service"): one thread owns the
// NamespaceRelay, so its single-thread rule holds by construction. It syncs the configured names from
// the upstream every interval and answers the relay's HTTP routes from the mirror: W5 sessions with
// W6 frames, and the layouts and files a downstream relay reads.
namespace detent::relay {
struct MirrorServiceOptions {
	std::filesystem::path directory;  // the mirror's own, mode 0700
	distribution::Identity cluster{}; // the authority's, as its cursors carry it
	std::vector<std::string> namespaces; // the names mirrored, strictly increasing
	std::chrono::milliseconds interval{250}, idle{60000};
	std::uint64_t maximum_wait_ms = 30000;
	std::size_t sessions = 1024;
};
// One request the mirror answers, shared by the HTTP task that waits and the mirror thread.
struct MirrorJob {
	enum class Kind { Open, Next, Close, Layouts, File } kind = Kind::Open;
	distribution::SubscriptionPrincipal principal; // the credential's, which owns its sessions
	const distribution::NamespaceGrants *grants = nullptr;
	distribution::Identity session{};
	std::vector<Position> positions; // Open and Next; Layouts: the names, no cursors
	std::string name_space, name;    // File
	bool all = false, compressed = false;
	std::uint64_t wait_ms = 0;
	// The answer, set by the mirror thread before `done`: a JSON control body, or chunks.
	struct Reply {
		int status = 503;
		std::string body = "{\"error\":\"source_behind\"}";
		std::string content_type = "application/json";
		std::vector<std::pair<std::string, std::string>> headers;
		std::vector<std::shared_ptr<const distribution::OwnedBuffer>> chunks; // non-empty: streamed
	} reply;
	std::atomic<bool> done{false}, abandoned{false};
};
struct MirrorStatus {
	bool ready = false, failed = false; // a first sync done; the mirror stopped (`error` says why)
	std::uint64_t syncs = 0, sync_failures = 0, entries = 0;
	std::uint64_t fetched_files = 0, fetched_bytes = 0, reused_files = 0, collected_files = 0;
	std::uint64_t answers = 0, idle = 0;
	std::size_t sessions = 0, failed_namespaces = 0;
	std::string error;
};
class MirrorService {
  public:
	// `upstream` is called on the mirror thread only. Answers are allocated from `account`.
	MirrorService(MirrorServiceOptions, std::unique_ptr<Upstream>, std::shared_ptr<distribution::StorageLedger>);
	~MirrorService(); // stops and joins the thread
	MirrorService(const MirrorService &) = delete;
	MirrorService &operator=(const MirrorService &) = delete;
	// Thread-safe. The job is answered once (`done`); an abandoned one may be dropped unanswered.
	void submit(std::shared_ptr<MirrorJob>);
	void close() noexcept;
	MirrorStatus status() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
