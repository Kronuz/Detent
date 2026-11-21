# consensus-peers-wake: journal

State: working
Quest: .campaign/detent/quests/consensus-peers-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Local dev build of consensus_transport_peers_wake_test at consensus:3576fb5 (quiet light
peers-wake-dev, .scratch/build in the worktree); then fix what it finds.

## Next

1. Green locally, then jobs: A1 suite (`campaign verify --for A1,A5`), A2 mutants from
   test/peers_wake_mutants.py (refs/mutants/consensus-peers-wake/<name>), A3 TSan on the
   transport Peers tests.
2. Push the quest branch for CI (A5).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- R1: merged consensus main 8780e44 (consensus-driver-install landed) before any work: a
  fast-forward, so CMakeLists.txt and CHANGELOG.md carry its hunks under mine.
- R2: owner work = what the owner loop (admit, Worker and channel run_one, pump) must act on.
  Paths that wake: Control input retained; Bulk frame the channel took bytes from or ran
  (consumed > 0 or Accepted); Bulk frame refused closing an open session (Closed and
  session_open before); snapshot frame consumed by the writer; Bulk session registered and
  open; open Bulk session lost (reader/writer end, run end, fetch's second deadline).
- R3: not owner work, no wake: Control lanes opening/closing (the Worker never learns of them),
  dials, fetch frames (Peers serves them), fetch completions (`done` is the host's callback).
- R4: never from a host call (start, stop, send, pump, fetch, admit): close() returns whether a
  session was lost and only Peers' own completions (`end()`) wake on it.
- R5: wake is noexcept at the call site (a throwing wake terminates); the host may call back
  into Peers from it (the tests admit and pump from inside it).
- R6: test/peers_wake_mutants.py reuses test/transport/mutant_commit.py's plumbing by import
  (that file is outside the file set) with its own mutant table.
