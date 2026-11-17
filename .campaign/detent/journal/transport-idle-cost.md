# transport-idle-cost: journal

State: working
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Attributing the bytes. consensus:69890f8 has `bench/transport_idle.cc` (cost, hop; ctest
`consensus_transport_idle` runs `cost --quick`; fixture gains nodes four, five). Local smoke runs
(strict holds, not evidence) on this Mac (i9-9980HK, macOS 15.8.1, OpenSSL 4.1.0):

- Per idle group: 0 fds, 0 timers, every frame delivered; C++ heap 0.0 B/group, but zone stats
  +36..56 B/group in ~2 non-C++ blocks per group (experiment 1: nano zone on/off).
- Per connection end: 69-86 KB zone (C++ 20-37 KB of it, incl. the per-loop task wheel, one
  >=64 KiB block per loop); host 166-181% of `29 KB + 2P x 41 KB`. OpenSSL alone: 40,732 B/end,
  context 29,408 B (Detent job 261117-082142-0a2ca2, conn_cost 64, same OpenSSL 4.1.0).
- H=5 at 10,000 groups: connections die mid-run (links 40 -> 46..61). `sample`: loops pegged in
  `HostTransport::feed` (11,387 of ~15k samples): `next()` scans `lane.groups` per frame and
  `erase_if` per feed, O(groups) each, while groups wait for credit; the loop stalls past
  `dead_after`. A transport bug outside the file set: goes to Blocked on.
- Hop smoke (1 round): stream control +156 ns/frame (A/A 5), data +201 (A/A 25), alone +26.6 us
  per wake (A/A 3.3k); receiver 85 us CPU per lone frame (madvise/nanov2 churn in the profile).

## Next

1. Finish attribution (experiment 1), then the evidence jobs: `cost` and `hop 7` exclusive with
   `--build`, and Detent `run.sh hop_cost` beside it.
2. docs/perf/transport-idle-cost.md; Blocked on: the feed scan, and budgets over.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
