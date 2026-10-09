---
title: "Raft Clustering"
---

The classic distributor's clustering uses `cluster::Raft` from
[Kronuz/cluster](https://github.com/Kronuz/cluster), the same library Xapiand
uses, over `cluster::Bus` UDP multicast.

---

## Write Path

```d2 alt="Any node accepts a write and wraps it in a command envelope; Raft forwards it to the leader, commits it on a quorum, and every node applies it through resolve_and_commit; the originating node completes the waiting HTTP request"
direction: right
client: "Publisher" {shape: person}
follower: "Node B\n(follower)"
leader: "Node A\n(leader)"
c: "Node C\n(follower)"
client -> follower: "PUT /v1/snapshot"
follower -> leader: "add_command()\nforwarded"
leader -> follower: "AppendEntries"
leader -> c: "AppendEntries"
follower -> client: "200 after commit\n+ X-Detent-Leader"
```

1. Any node accepts `PUT` or `PATCH`. `submit_command()` wraps the body in an
   envelope with a correlation ID and calls `raft_->add_command()`, which
   forwards to the leader if needed.
2. Once the entry commits on a quorum, `on_raft_apply()` runs on **every**
   node, including the one that received the request, and applies it through
   the same `resolve_and_commit()` a standalone node uses.
3. The originating node matches the correlation ID to its pending request and
   answers it with the real result.

Because every node applies the identical committed sequence in the same order,
every node has identical snapshots, ETags and diff histories. A client can
read from any node, and fail over between nodes, without invalidating its ETag
or diff base.

---

## Safety Details

### Restart Silence

`cluster::Raft` keeps `current_term` and `voted_for` in memory. A restarted
node could otherwise vote again in a term it already voted in. Detent's Raft
delegate reports the node inactive for `3 × election_max` after a restart: it
can't vote, be voted for, or start an election. Since every live node bumps
its term within one `election_max` without a leader, no term the restarted node
might have voted in is still open when the window ends. A
`<persist_path>.cluster_marker` file distinguishes a first launch, which skips
the window, from a restart.

### Liveness

Raft picks the next log entry to broadcast from the minimum `next_index` among
peers it believes alive. A permanently dead peer reported alive would freeze
that choice and stall commits. The delegate tracks when it last heard from each
peer and reports it dead after `4 × heartbeat_timeout`.

### Correlation IDs

Correlation IDs combine the node address, a random per-process session ID and
a counter. Without the session ID, a node replaying its pre-crash log after a
restart could match an old entry to a new request with the same counter. The
end-to-end chaos harness found that bug; the fixed-timing in-process test
couldn't.

### Shutdown Order

The distributor stops the bus before destroying Raft, so the bus's reactor
thread can't deliver a message into a Raft instance that's being torn down.
ThreadSanitizer found the original ordering bug.

---

## Testing

1. `test_raft_cluster` runs a real three-node cluster in one process: election,
   follower-forwarded writes, patch replication, leader failover, restart and
   rejoin, dead-follower liveness, and correlation-ID uniqueness across restart.
2. `tests/e2e/` spawns real `detentd` processes, drives them over HTTP, and
   kills them with real signals.

---

## Why a Durable Profile Exists

The silence window is a pragmatic fix for an infrequent-write workload, not
durable consensus. The [durable authority](/Detent/architecture/authority/)
uses a separate, strict consensus module with persisted terms, votes and logs,
point-to-point TLS transport and fixed membership, while leaving this legacy
module and its wire format untouched for Xapiand.
