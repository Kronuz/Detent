#pragma once

// The distribution-layer server: Section 10's KnobSource extension seam
// ("Any publisher that emits valid snapshots"), made concrete as a runnable
// HTTP service instead of a hand-written test fixture.
//
// Deliberately NOT a "Detent control plane" in the Section 15 sense (no
// management UI, no ownership/audit catalog, no approval workflow) -- it is
// exactly what the spec's own non-goals table says IS in scope for a
// publisher: accept a validated snapshot, serve it, stay out of the way.
//
// Two read paths, on purpose:
//   - GET /v1/snapshot   the ALWAYS-instant, ETag-conditional read. handle()
//     never blocks; a repeat poll from an unchanged client costs a body hash
//     comparison and a 304, not the payload. This is what scales: reading
//     Kronuz/http's own connection code (detail::serve_connection) shows
//     handle() must synchronously call end() before returning -- there is no
//     deferred-completion path in the current framework -- so a genuine
//     server-held long-poll would pin one worker-pool thread per WAITING
//     watcher for the duration of its wait. That is a real, bounded-capacity
//     pattern (fine for a fleet sized to the pool), but it is not what
//     "efficient at scale" should default to when polling gets the same
//     eventual-freshness contract (Section 3: "eventual; not transactional")
//     essentially for free. So: poll, not push.
//   - GET /v1/health     small liveness/diagnostics JSON, for operators and
//     tests.
//
// Cross-replica propagation: with Options::cluster set, the distributor
// runs cluster::Raft over a cluster::Bus multicast channel. Only the
// elected leader's writes commit; a write isn't "accepted" until a quorum
// has it. See CLUSTERING.md for the full design, the multi-region shape,
// and why the old best-effort gossip is gone.

#include "detent/json_patch.hh"
#include "detent/json_schema.hh"
#include "detent/model.hh"
#include "detent/owner_loop.hh"
#include "raft_delegate.hh"

#include <http_handler.h>
#include <http_router.h>
#include <raft.h>
#include <random.hh>
#include <generation_cache.hh>

#include <atomic>
#include <atomic_shared_ptr.h>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace cluster { class Bus; }

namespace detentd {

struct Options {
	// Optional application diagnostics; runtime errors use the logger hook.
	std::function<void(std::uint64_t)> on_accept;
	std::function<void(detent::UpdateRejectReason)> on_reject;
	// Called on the owner thread: before each commit (a test latch) and with each commit's duration.
	std::function<void()> on_commit_begin;
	std::function<void(std::uint64_t, std::chrono::nanoseconds)> on_commit;
	// Where the current accepted snapshot is durably persisted (atomic
	// write + rename), so a restart resumes at the last accepted version
	// instead of regressing to "no snapshot" (Section 9's "keep
	// last-known-good" applied to the distributor's own restart, not just
	// the client's).
	std::filesystem::path persist_path;
	std::size_t max_knobs = 100'000;
	// How many single-step patches route_get_snapshot can compose from for
	// a poller more than one version behind -- see diff_history_'s comment.
	// Higher retains more history (a poller can fall further behind and
	// still get a diff instead of the full body) at the cost of more
	// memory for diff_history_ and a larger composed_diff_cache_ working
	// set; lower bounds both tighter. 64 matches this server's original,
	// previously-hardcoded default.
	std::size_t max_diff_history = 64;

	struct Cluster {
		std::string token;              // cluster::Bus token; every member must match exactly
		unsigned short multicast_port = 0;
		std::string multicast_group;     // e.g. "239.255.0.1"
		std::string multicast_interface; // pin e.g. "127.0.0.1" for same-host testing
		// This node's OWN advertised "host:port" HTTP endpoint -- Raft
		// membership identity, and what a follower's X-Detent-Leader
		// response header names once this node IS the leader.
		std::string self_address;
		// Every OTHER member's "host:port" HTTP endpoint. Deliberately
		// static configuration, not inferred/discovered: on one host,
		// every replica shares the same source IP but a different port, so
		// "sender address" alone cannot address them, and this project's
		// existing convention is an explicit list either way.
		std::vector<std::string> peers;
		// Overridable for tests (Raft's real-world defaults -- 1s
		// heartbeat, 10-30s election timeout -- are far too slow for a
		// test suite that wants an election to resolve in well under a
		// second); production deployments should leave this at the
		// library's own defaults.
		cluster::RaftConfig raft_config{};
	};
	std::optional<Cluster> cluster;
};

class Distributor : public http::HttpHandler {
public:
	explicit Distributor(Options options);
	~Distributor() override;

	Distributor(const Distributor&) = delete;
	Distributor& operator=(const Distributor&) = delete;

	// Every route is a task on the connection's loop. GETs never suspend; PUT and PATCH hop to the
	// owner, wait there for the commit, and come home with the reply (D2, D13).
	task::task<void> handle(http::Request& request, http::ResponseWriter& response) override;
	// A PUT or PATCH takes one of kWriteSlots write slots for as long as the request lives; with none
	// free the request is refused before any body byte (D3).
	std::unique_ptr<http::RequestExtension> create_extension(const http::Request& request) override;

	// Ends every owner wait with its truthful reply (D10). Call before the HTTP service stops.
	void begin_shutdown() noexcept;
	// The owner of serialized snapshot writes; its D15 phases belong to the process (main.cc).
	[[nodiscard]] detent::OwnerLoop& owner() noexcept { return owner_; }

	static constexpr std::size_t kWriteSlots = 1024;

	// True once a committed entry could not be persisted on this node: it keeps serving what it has and
	// refuses writes, because Raft no longer retries an apply (D13).
	[[nodiscard]] bool is_fenced() const noexcept { return fenced_.load(std::memory_order_acquire); }

	// Diagnostics for tests; not part of the HTTP surface.
	[[nodiscard]] std::uint64_t current_version() const;
	[[nodiscard]] std::uint64_t requests_served() const noexcept { return requests_served_.load(); }
	[[nodiscard]] std::uint64_t puts_accepted() const noexcept { return puts_accepted_.load(); }
	[[nodiscard]] std::uint64_t puts_rejected() const noexcept { return puts_rejected_.load(); }
	// How many GETs were answered with the cached diff instead of the full
	// body (proves the bandwidth optimization engaged, not just "didn't crash").
	[[nodiscard]] std::uint64_t diffs_served() const noexcept { return diffs_served_.load(); }
	// How many of those diffs_served() required ACTUALLY composing
	// diff_history_ entries (a cache miss), as opposed to reusing
	// composed_diff_cache_ or the single-step current_diff_bytes_ fast
	// path -- proves the composed-diff cache engaged, not just that
	// results were correct (which could be true even recomputing every
	// time).
	[[nodiscard]] std::uint64_t diffs_composed() const noexcept { return diffs_composed_.load(); }
	// Unclustered: always true. Clustered: true only on the elected leader.
	[[nodiscard]] bool is_leader() const;
	// Best-known leader address ("" if unknown/unclustered/mid-election).
	[[nodiscard]] std::string leader_address() const;

	// Called by DetentRaftDelegate on Raft's callback path; public because
	// it's invoked from a different translation unit.
	void on_raft_leader_changed(std::string leader_address);
	void on_raft_apply(const std::string& command);

private:
	task::task<void> route_write_snapshot(http::Request&, http::ResponseWriter&, bool is_patch);
	task::task<void> route_get_snapshot(http::Request&, http::ResponseWriter&);
	task::task<void> route_get_health(http::Request&, http::ResponseWriter&);

	struct Published;
	struct Write;
	struct WriteSlot;

	// Where a PUT and a PATCH both land, on the owner thread only: `resolve` runs on the owner's
	// current state (which is what makes a PATCH's merge race-free) and returns the new full
	// Snapshot. Persists, builds the immutable Published and swaps it in. The only place state is
	// ever mutated -- the unclustered path and Raft's on_raft_apply() both call into it.
	using SnapshotResolver = std::function<detent::Result<detent::Snapshot, detent::UpdateError>(
		const detent::Snapshot& current, bool has_current)>;
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> resolve_and_commit(SnapshotResolver resolve);

	// Validates + (if newer) accepts `bytes` as a full replacement snapshot.
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> try_accept(std::string_view bytes);

	// Validates + (if newer, and if a snapshot already exists to patch) merges `patch_bytes` onto the
	// current snapshot. Same acceptance path as try_accept (persist/etag/diff) -- just arriving at
	// the new full state via a merge instead of a full replace.
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> try_accept_patch(std::string_view patch_bytes);

	// The write entry point BOTH PUT and PATCH call, a task the handler awaits: it hops to the owner.
	// Unclustered, the owner applies `body` directly via try_accept/try_accept_patch. Clustered, the
	// owner submits it through Raft's replicated log and the wait ends when on_raft_apply() reports
	// the outcome, a stop, or the 10 s backstop -- so the HTTP response still means "a quorum has
	// this", not just "I queued it". `is_patch` selects try_accept vs. try_accept_patch once the
	// bytes reach on_raft_apply(), on whichever node applies them.
	[[nodiscard]] task::task<detent::Result<std::uint64_t, detent::UpdateError>> submit_command(
		std::string_view body, bool is_patch);

	void apply_envelope(const std::string& command); // owner thread

	void compress_into(std::string_view identity, std::string& gzip_out, std::string& zstd_out);
	void write_negotiated(
		const http::Request& request, http::ResponseWriter& response, int status,
		const std::string& identity, const std::string& gzip_body, const std::string& zstd_body,
		std::string_view content_type);

	void persist(std::string_view bytes, std::uint64_t version); // owner thread
	void load_persisted();
	void on_bus_message(int wire_type, std::string_view content);

	Options options_;
	http::Router router_;

	// The immutable published state: GET and health load the pointer and never wait on a commit.
	// Written by the owner thread only (resolve_and_commit), which keeps the mutable working copy
	// below.
	atomic_shared_ptr<const Published> published_;
	detent::Snapshot current_snapshot_; // owner thread only
	std::size_t max_diff_history_ = 64;

	std::atomic<std::uint64_t> requests_served_{0};
	std::atomic<std::uint64_t> puts_accepted_{0};
	std::atomic<std::uint64_t> puts_rejected_{0};
	std::atomic<std::uint64_t> diffs_served_{0};
	std::atomic<std::uint64_t> diffs_composed_{0};
	std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};

	std::atomic<std::size_t> write_slots_{0};
	std::atomic<bool> stopping_{false};
	std::atomic<bool> fenced_{false};
	task::cancel_source stop_;

	// Declared before bus_: the Bus' reactor thread spawns applies on the owner, and ~Distributor
	// stops the Bus before the owner goes.
	detent::OwnerLoop owner_;

	std::unique_ptr<cluster::Bus> bus_;
	std::unique_ptr<DetentRaftDelegate> raft_delegate_;
	std::unique_ptr<cluster::Raft<RaftNode>> raft_;

	// Clustered writes in flight, keyed by correlation id; owner thread only (apply runs there too).
	std::map<std::string, std::shared_ptr<Write>> pending_;
	std::atomic<std::uint64_t> command_counter_{0};

	// command_counter_ restarts from 0 every process launch, so a bare address+counter id can
	// collide with one this same node used BEFORE a crash -- a restarted node replays its whole
	// prior Raft log during catch-up, and a replayed old entry can then steal a freshly-registered
	// pending slot meant for a brand new write, handing its waiter a stale result. session_id_
	// (random, chosen fresh per process) makes every id unique across restarts.
	const std::uint64_t session_id_ = random_int(1, UINT64_MAX);

	mutable std::mutex leader_mutex_;
	std::string leader_address_;
};

} // namespace detentd
