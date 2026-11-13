# namespace-s5-3: journal

State: working
Quest: .campaign/detent/quests/namespace-s5-3.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A2-A5 code is in (async history writes, F1 table rot, F6, F7, mutant script); local runs pass.
A1's transport is waiting on the lead (msg-44 to the lead: where the peer fetch endpoint lives).

## Next

1. Local authority suite; then jobs for A2-A5 at the branch head.
2. A1 once the lead rules: the production `history::Fetch` and its endpoint, a cluster test over
   the real transport (three nodes, `AuthorityPeers`-style TLS, empty history directory installs).
3. docs/status/namespace-s5.md S5.3 section; the seven S5.2 mutants and the new ones unwrapped.

## Blocked on

A1's endpoint: msg-44 to the lead (options: a fetch lane in consensus's peer transport,
recommended; `GET /v2/history/<name>` on the publisher listener; a dedicated history TLS listener).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1 | <pending the lead's ruling> |
| A2 | <jobs to submit> |
| A3 | <jobs to submit> |
| A4 | <jobs to submit> |
| A5 | <jobs to submit> |

## Result

<When ready for review.>

## Rulings I made

1. History writes are a coroutine `IOOperation` (`history.cc` `run`) that the host drives like
   the worker's storage jobs: the cluster harness and tests synchronously, the service on its own
   completion queue and driver beside the journal's. One write per seal: seal file, outputs,
   table, collector, in that order; writes run strictly in seal order.
2. Apply's seal still computes the layout (CPU, and reads of files the layout reaches, served from
   queued writes first); only writes leave it. Apply waits once `Node::history_backlog` (4) writes
   are queued, so memory stays bounded when the device is slow.
3. `durable_through` is the newest settled write's seal (F4); the node keeps the states of queued
   seals and takes checkpoints at `durable_seal()`, the newest durable one; the ring covers through
   its revision.
4. The collector's keep set is computed in memory: what the write's table reaches, plus the seal
   files above it and their outputs that the layout holds. Seal files a pre-crash run left above the
   current seal are removed instead of kept; replay rewrites them with the same bytes.
5. `Fetch` is asynchronous (`done` on the owner). Recovery and install run their synchronous logic
   against local files plus what was fetched; a missing name aborts the attempt, every missing name
   found is fetched, and the attempt runs again once they land. Their writes queue as one write.
   Apply waits while a restore fetches; an install's activation waits until its write is durable.
6. A rotten `layout` table is treated as missing (F1): a peer's copy, else the ordinal-0 path,
   else recovery stops.
7. F6: `check_retained_bounds` refuses when `consensus_limits.log_entries/log_bytes` are below the
   policy's, beside the retained bounds.
8. F7: the history cluster test keeps journals in `MemoryIO` and stubs history syncs
   (`testing::cached_storage`); crash safety is the power-cut tests'. 52 s to 0.6 s locally.
9. The power-cut test splits into three ctest entries (`cuts`, `lagged`, `installs rot`) so each
   stays well inside a sanitizer job's per-test budget.
