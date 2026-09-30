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
#include "raft_delegate.hh"

#include <http_handler.h>
#include <http_router.h>
#include <raft.h>
#include <random.hh>
#include <generation_cache.hh>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace cluster { class Bus; }

namespace detentd {

struct Options {
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

	void handle(const http::Request& request, http::ResponseWriter& response) override;
	// GETs are cheap and non-blocking (in-memory read under a shared lock);
	// keep them inline on the reactor. PUT/PATCH do file I/O, validation,
	// and (when clustered) waiting on consensus, so they get the default
	// (offloaded to the worker pool).
	bool should_offload(const http::Request& request) const override;

	// Section 4's own oversize/parse-limit posture ("reject before allocation
	// exceeds configured cap"), applied to the transport layer: the http
	// framework's default buffered-body path has NO built-in size limit --
	// confirmed by reading http_request_parser.h -- so an unbounded PUT
	// Content-Length would otherwise be buffered into memory in full before
	// json_schema's own kDefaultMaxBytes check ever runs. This caps it at the
	// only hook that fires before buffering (headers-complete).
	[[nodiscard]] std::unique_ptr<http::BodySink> on_request_body(http::Request& request) override;

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
	void route_put_snapshot(const http::Request&, http::ResponseWriter&, const http::Params&);
	void route_patch_snapshot(const http::Request&, http::ResponseWriter&, const http::Params&);
	void route_get_snapshot(const http::Request&, http::ResponseWriter&, const http::Params&);
	void route_get_health(const http::Request&, http::ResponseWriter&, const http::Params&);

	// Where a PUT and a PATCH both land: `resolve` runs WHILE
	// snapshot_mutex_ is held exclusively, given the current locked state,
	// and returns the new full Snapshot. Persists, caches ETag/compression,
	// and caches the diff. The only place state is ever mutated -- the
	// unclustered fast path and Raft's on_raft_apply() both call into it.
	using SnapshotResolver = std::function<detent::Result<detent::Snapshot, detent::UpdateError>(
		const detent::Snapshot& current, bool has_current)>;
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> resolve_and_commit(SnapshotResolver resolve);

	// Validates + (if newer) accepts `bytes` as a full replacement snapshot.
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> try_accept(std::string_view bytes);

	// Validates + (if newer, and if a snapshot already exists to patch)
	// merges `patch_bytes` onto the current snapshot. Same acceptance path
	// as try_accept (persist/etag/diff) -- just arriving at the new full
	// state via a merge instead of a full replace, so a caller only has to
	// send the one or few knobs that actually changed.
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> try_accept_patch(std::string_view patch_bytes);

	// The write entry point BOTH route_put_snapshot and route_patch_snapshot
	// call: unclustered, applies `body` directly via try_accept/
	// try_accept_patch (today's single-node behavior, unchanged). Clustered,
	// submits it through Raft's replicated log (rejecting outright if this
	// node isn't the elected leader) and BLOCKS the calling worker thread
	// (already off-reactor; see should_offload) until on_raft_apply()
	// reports the outcome or a timeout elapses -- so the HTTP response
	// still means "a quorum has this", not just "I queued it". `is_patch`
	// selects try_accept vs. try_accept_patch once the bytes reach
	// on_raft_apply(), on whichever node applies them (this one, if
	// unclustered or leader; every member, once Raft commits, if
	// clustered).
	[[nodiscard]] detent::Result<std::uint64_t, detent::UpdateError> submit_command(
		std::string_view body, bool is_patch);

	void compress_into(std::string_view identity, std::string& gzip_out, std::string& zstd_out);
	void write_negotiated(
		const http::Request& request, http::ResponseWriter& response, int status,
		const std::string& identity, const std::string& gzip_body, const std::string& zstd_body,
		std::string_view content_type);

	void persist_locked(std::string_view bytes, std::uint64_t version); // caller holds snapshot_mutex_ exclusively
	void load_persisted();
	void on_bus_message(int wire_type, std::string_view content);

	Options options_;
	http::Router router_;

	mutable std::shared_mutex snapshot_mutex_;
	std::string current_bytes_;
	std::uint64_t current_version_ = 0;
	bool has_snapshot_ = false;
	// Cached alongside current_bytes_/current_version_, computed once per
	// accepted update rather than once per GET: the http library's own
	// automatic conditional-GET support (enable_conditional()) hashes the
	// *entire response body on every single request* to derive its ETag,
	// found by performance review to cost ~577us/request hashing alone at a
	// 560KB snapshot (~2.8 cores at 5K req/s just to rediscover the same
	// validator). Setting our own "ETag" header before returning makes the
	// library skip its own computation entirely (it explicitly checks
	// has_header("ETag") first) -- see route_get_snapshot().
	std::string current_etag_;
	// Pre-compressed forms of current_bytes_, cached alongside it (see
	// compress_into's comment): empty if not computed/not worth it, in
	// which case a request negotiating that coding falls back to identity.
	std::string current_bytes_gzip_;
	std::string current_bytes_zstd_;

	// The parsed form of current_bytes_, kept so a PATCH request has
	// something to merge onto and so the NEXT accepted update can diff
	// against it (diff_snapshots needs two structured Snapshots, not two
	// byte strings).
	detent::Snapshot current_snapshot_;
	// A cached JSON Merge Patch (see json_patch.hh) from
	// current_diff_base_version_ to the current one, precomputed and
	// pre-compressed at accept time so the common "one update behind"
	// poller costs nothing extra per request. A poller further behind
	// composes from diff_history_ instead (route_get_snapshot); this
	// single cached entry is a fast path, not the only path.
	bool has_diff_ = false;
	std::uint64_t current_diff_base_version_ = 0;
	std::string current_diff_bytes_;
	std::string current_diff_bytes_gzip_;
	std::string current_diff_bytes_zstd_;

	// One entry per accepted update: the patch from base_version to the
	// next accepted version, in order. Bounded to max_diff_history_
	// entries (oldest evicted first) so memory stays fixed regardless of
	// uptime. A poller whose last-known version is >= the oldest retained
	// base_version gets a diff composed from these entries instead of the
	// full body, however many versions behind it is, up to that bound.
	struct StepDiff {
		std::uint64_t base_version;
		detent::SnapshotPatch patch;
	};
	std::size_t max_diff_history_ = 64;
	std::deque<StepDiff> diff_history_;

	// Composing N step-diffs for a given base_version (route_get_snapshot)
	// does real work -- parsing/merging N patches -- and every poller still
	// behind the SAME base_version between two accepted updates needs the
	// identical result. Cached here, keyed by base_version, and entirely
	// cleared on the next accepted update (resolve_and_commit): every
	// entry's result is relative to the OLD current_version_, so none of
	// them are valid once that changes. At most max_diff_history_ distinct
	// keys are ever reachable (anything older already falls back to the
	// full body), so this cache is implicitly bounded by the same option.
	//
	// generation_cache::Cache (not a hand-rolled map+mutex) is what makes
	// this safe against a real race: a reader composes a diff while
	// holding snapshot_mutex_ shared, but only serializes/compresses it
	// (the expensive part) AFTER releasing that lock -- so a NEW accepted
	// update, and the clear() that comes with it, can land in between.
	// try_insert() silently drops a result whose captured generation no
	// longer matches current, instead of resurrecting a value relative to
	// the OLD current_version_ -- see generation_cache's own README/
	// ARCHITECTURE.md for the general case this solves.
	struct CachedComposedDiff {
		std::string bytes;
		std::string bytes_gzip;
		std::string bytes_zstd;
	};
	generation_cache::Cache<std::uint64_t, CachedComposedDiff> composed_diff_cache_;

	std::atomic<std::uint64_t> requests_served_{0};
	std::atomic<std::uint64_t> puts_accepted_{0};
	std::atomic<std::uint64_t> puts_rejected_{0};
	std::atomic<std::uint64_t> diffs_served_{0};
	std::atomic<std::uint64_t> diffs_composed_{0};
	std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};

	std::unique_ptr<cluster::Bus> bus_;
	std::unique_ptr<DetentRaftDelegate> raft_delegate_;
	std::unique_ptr<cluster::Raft<RaftNode>> raft_;

	// Bridges submit_command()'s blocking wait to on_raft_apply()'s
	// callback (Bus reactor thread): a correlation id keys a slot the
	// waiter registers before add_command(); apply() fills it in and
	// notifies if a match exists (only the originating node has one).
	// Mutex/condvar, not promise/future, since a timed-out waiter reclaims
	// its own slot.
	struct PendingCommand {
		bool done = false;
		detent::Result<std::uint64_t, detent::UpdateError> result{detent::UpdateError{
			detent::UpdateRejectReason::Io, "no result recorded"}};
	};
	std::mutex pending_mutex_;
	std::condition_variable pending_cv_;
	std::map<std::string, PendingCommand> pending_commands_;
	std::atomic<std::uint64_t> command_counter_{0};

	// command_counter_ restarts from 0 every process launch, so a bare
	// address+counter id can collide with one this same node used
	// BEFORE a crash -- a restarted node replays its whole prior Raft
	// log during catch-up, and a replayed old entry can then steal a
	// freshly-registered pending_commands_ slot meant for a brand new
	// write, handing its waiter a stale result. session_id_ (random,
	// chosen fresh per process) makes every id unique across restarts.
	const std::uint64_t session_id_ = random_int(1, UINT64_MAX);

	mutable std::mutex leader_mutex_;
	std::string leader_address_;
};

} // namespace detentd
