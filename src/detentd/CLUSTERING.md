# detentd clustering

## Consensus

`cluster::Raft` (vendored via the `cluster` dependency) provides leader
election and a replicated log across `detentd` nodes over `cluster::Bus`
multicast. Only the elected leader's writes commit; a write is accepted
once a quorum acknowledges it. There is no separate replication mechanism:
a node applies a committed log entry directly.

## Wire format

Raft's 7 message types ride `cluster::Bus` wire types 1..7
(`kRaftWireTypeBase` + the `RaftMessage` enum value). `kMaxRaftWireType` is
`Bus`'s `max_type`.

## Write path

Any node accepts an HTTP PUT/PATCH. `submit_command()` wraps the body in an
envelope (correlation id + PUT/PATCH marker + raw body) and calls
`raft_->add_command()`, which forwards to the current leader if the local
node isn't it. `on_raft_apply()` runs on every member once the entry
commits, including the originating node; `pending_commands_` bridges that
callback back to the waiting HTTP request via the correlation id.
`resolve_and_commit()` performs the actual merge/persist/etag/diff work and
is the same function for the unclustered path and every `apply()` call.

## `prefers()`

`RaftDelegate::prefers(a, b)` must be reflexive: `on_request_vote_response()`
calls `prefers(local, voted_for_node)` to detect a self-vote, so
`prefers(x, x)` must be `true`. `DetentRaftDelegate::prefers` returns
`a.address == b.address`, matching `cluster`'s `mem::Delegate` reference
implementation. Related: https://github.com/Kronuz/cluster/pull/1.

## Restart safety

`cluster::Raft` keeps `current_term_`/`voted_for_` in memory only; a
restarted node has no record of votes it already cast. `DetentRaftDelegate`
addresses this without persisting Raft state: `active()` returns `false`
for `3 * election_max` after construction, and every Raft entry point in
`raft.h` checks `active()` first. A restarted node cannot vote, be voted
for, or start an election during that window. Since every live node bumps
its term within one `election_max` if no leader confirms itself, no term
the restarted node could have stale-voted in remains open once the window
elapses; the 3x factor covers propagation/scheduling jitter.

This window only protects against a stale vote from a PRIOR run, so it
must not apply to a node's genuine first-ever launch (no prior vote
exists to be stale). A `<persist_path>.cluster_marker` file distinguishes
the two: absent on first launch (silence is zero, then the file is
created), present on every subsequent restart (full silence). Without
this, a fresh cluster bootstrap waits out the full window before its
first election -- e.g. 90s at the CLI's default `RaftConfig`.

Tradeoff: a restarted node cannot participate in quorum for that window.
Acceptable for Detent's write pattern (infrequent config changes); a
higher-write-rate system should use real term/vote persistence instead.

## Multi-region

Not implemented. One Raft cluster per region; one region is primary. Each
secondary region's leader runs an upstream `detent_http` poller against the
primary (the same diff/patch-aware polling `detent::Client` performs) and
re-submits received updates through its own local `add_command()`. Each
region retains its own consensus and failover; the region-to-region link is
asynchronous and does not add latency to a region's local write path.

## resolve_and_commit locking

`apply_patch` runs while `snapshot_mutex_` is held exclusively, using the
currently locked state as the merge base. Merging outside the lock (read,
release, merge, re-lock to commit) would let two concurrent PATCHes merge
onto the same base and the later commit would overwrite the earlier one;
the version-number check alone does not catch this, since it only rejects
a stale version number, not a stale merge base.

## Diff history and cross-node consistency

`resolve_and_commit` appends one step-diff (source version -> the patch
that produces the next version) to a bounded deque, `kMaxDiffHistory`
entries (oldest evicted first). A GET whose `X-Detent-Base-Version` falls
within that window gets a diff composed from the retained steps instead of
the full body, however many versions behind it is, up to that bound; older
or unrecognized base versions fall back to the full snapshot. Composing
patches without the full snapshot at either end cannot detect a knob that
changed and reverted within the window, so a composed diff can include a
redundant no-op entry for that knob -- applying it still reconstructs the
exact target, just not always at the theoretical minimum size.

Every Raft node applies the identical committed sequence in the identical
order, so every node's diff history is identical (same base versions, same
patches). A poller gets the same minimal diff regardless of which cluster
member answers the request.

## Shutdown order

`~Distributor()` calls `bus_->stop()` before `raft_`/`raft_delegate_`
destruct. `bus_`'s reactor thread delivers messages into `raft_` via
`on_bus_message()`; without an explicit `stop()` first, member destruction
order (reverse of declaration: `raft_` before `bus_`) tears down `raft_`
while that thread can still be running.

## Liveness detection

`cluster::Raft` requires `is_alive()`/`alive_nodes()` to reflect real
liveness, not just static configured membership -- `heartbeat_cb()` (in
`raft.h`) picks the log entry to broadcast next as the MINIMUM
`next_index` among `is_alive()` peers. A permanently-dead peer reported
alive forever freezes that pick at its last (stale) `next_index`, silently
blocking every later entry from ever reaching quorum, even with an
otherwise-healthy majority. `DetentRaftDelegate` tracks a last-seen
timestamp per peer, updated in `parse_node()` (called for every parsed
Raft message); `is_alive()` checks it against `liveness_timeout`
(`4 * heartbeat_timeout`), with a not-yet-contacted peer treated as alive
(benefit of the doubt before first contact).

## Correlation id uniqueness

`submit_command()` keys `pending_commands_` by a correlation id
(`self_address + "#" + session_id_ + "#" + command_counter_`) so `on_raft_apply()` can
route a committed entry's result back to the HTTP request that originated
it. `command_counter_` resets to 0 every process restart; a restarted node
replays its whole pre-crash log during catch-up, so without `session_id_`
a write it originates right after restarting can regenerate an id an
earlier (pre-crash) write already used. If that old entry is still
replaying when the new one registers its pending slot, the replay resolves
the new request with the old (stale) result instead of its own -- found via
`tests/e2e/`'s chaos scenario, not `test_raft_cluster`'s fixed, fast Raft
timing. `session_id_` is a random value chosen once per process
construction, making every restart's ids disjoint from every prior one.

## Which node a client reads from

Reads don't need the leader: every node applies the identical committed
log in the identical order (see "Diff history and cross-node consistency"
above), so a GET's ETag/diff is valid regardless of which member answers
it. Writes do need to reach the leader, but any node accepts one and
`add_command()` forwards it -- a write doesn't need to start there either.

`detent_http::make_http_poll_source`'s multi-node overload takes every
peer instead of one fixed host:port, picks one at random at construction
(so a fleet of independent clients spreads across the cluster rather than
piling onto node one), and stays on it -- preserving the one-reused-
keep-alive-connection design (see http_client.hh's own comment on why:
~5.5s p99 fan-out at 1,000 pollers each reconnecting per poll, versus
~100ms reusing one connection) -- until a request to it fails, then moves
to the next node in the list. The single-host overload still exists
unchanged for an unclustered `detentd` or a caller that wants to pin a
specific node itself.

