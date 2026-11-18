# namespace-s5-3: S5.2 follow-ups: peer fetch, async history I/O, review F1-F7

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s5-3`.

## Outcome

The authority's persisted history is usable in production before namespace-s6-cutover proposes the enabling entry: a voter installed from a snapshot fetches the history files it lacks from a peer over a real endpoint, and history I/O no longer runs synchronously inside apply's seal, with `durable_through` still exact. The non-blocking findings of namespace-s5-2's durability review (`.campaign/detent/reviews/namespace-s5-2-durability.md`) are each fixed or dropped with a reason.

## Acceptance criteria

- A1 F2: production peer fetch has an endpoint; a snapshot install on a voter with an empty history directory fetches every file from a peer and answers the same bytes: a cluster test over the real transport
- A2 F3, F4: history writes leave apply's seal (apply returns before the history I/O completes), and `durable_through` advances only behind synced history; a crash matrix over the asynchronous path recovers the same bytes: jobs, and the crash-model job
- A3 F1: a corrupted local `layout` table is repaired from a peer instead of stopping recovery, never serving wrong bytes: a test that fails without it
- A4 F6, F7: the retained-bounds check counts the node's own consensus limits (a test that fails without it), and `test_authority_history_cluster` runs well inside its timeout under sanitizers (CI green on every sanitizer job)
- A5 `tests/detent_authority/history_mutant.sh` exits 0 on a kill, as `--fails-without` expects, and its comment says so: the seven S5.2 mutants rerun through it unwrapped

## Contracts

Provides `namespace-blocks` (the peer fetch endpoint is part of it). F5 (an answer can fall in a ring gap with no signal) is namespace-s6-cutover's contract, not this quest's.

## Decisions

D90. Runs before namespace-s6-cutover's enabling proposal.

## May land early

A5, and A4's F7 test-time change.
