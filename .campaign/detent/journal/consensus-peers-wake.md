# consensus-peers-wake: journal

State: waiting on jobs 261121-212625-7909ca 261121-212627-ae7c2d 261121-212628-07693b and CI run 44989402380
Quest: .campaign/detent/quests/consensus-peers-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Correctness review accepted at consensus:7f1f7e1; all 13 jobs there ended as designed (A1 and
TSan passed, nine mutants and the probe failed). Merged consensus main e35356a (mutants per
ctest, fixes the red consensus_mutants CI job): consensus:a068ba0, a clean merge of
CMakeLists.txt and tools/mutants.sh; the peers_wake block is unchanged. Pushed; A1/A3 rerun at
the merge, CI run 44989402380 queued.

## Next

1. Record the A1/A3 reruns and CI at consensus:a068ba0; a red one gets its cause fixed.
2. The lead re-accepts the review at the merge.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261121-212625-7909ca (3 repeats), 261121-212627-ae7c2d (plain), at consensus:a068ba0; passed at consensus:7f1f7e1: 261121-143846-6e9ab7, 261121-143850-ef4b0f |
| A2 | 261121-143902-c2b9e9, 261121-143905-8c1df5, 261121-143908-546fd9, 261121-143912-fb2da8, 261121-143916-805a2e, 261121-143920-0a930f, 261121-143923-5e1a09, 261121-143927-f0a404, 261121-143931-b1fc2e, 261121-143950-b0ce0e (--fails-without at refs/mutants/consensus-peers-wake/<name> on consensus:7f1f7e1: control, bulk-taken, bulk-refused, consumed, opened, lost, timer, any-lane-opened, stop-wakes; the reviewer's probe at bulk-refused), all failed as designed |
| A3 | 261121-212628-07693b (macOS llvm TSan) at consensus:a068ba0; passed at consensus:7f1f7e1: 261121-143853-28897a |
| A4 | docs/mechanisms/peers-wake.md (consensus:3576fb5, bulk step consensus:7f1f7e1) |
| A5 | `ci:linux-tsan`, `ci:linux-asan-ubsan`, `ci:freebsd-x86_64`, `ci:macos-arm64`, `ci:macos-x86_64`, `ci:linux-arm64-gcc`: all green at consensus:a068ba0 (run 44989402380) |

Superseded at consensus:a1855ac, never run: A1 261121-140831-f20fe2 and 261121-140921-85a62f, A3
261121-140924-7f1c2c (replaced by the jobs above); the nine old mutant jobs 261121-140841-25783f,
261121-140844-0c16f8, 261121-140848-6433dd, 261121-140852-1056ff, 261121-140855-706a5f,
261121-140858-daa2fb, 261121-140901-80707b, 261121-140904-b8f618, 261121-140907-8c4cb2 cancelled
(their mutants sit on a1855ac, which lacks the new step; 6433dd could never fail there).

## Fixes since the review

| Finding | Fix | Test that fails without it |
|---|---|---|
| F1 | consensus:7f1f7e1: the bulk case's last step holds a Begin busy (the first transfer, canceled with its session, still waits on the Worker), checks one wake for its bytes and none over 200 ms of retries with the lane still open, closes the Worker, and checks exactly one wake for the refused retry (consumed 0, session open) and none as the lane ends. Mechanism doc updated. | bulk-refused mutant 261121-143908-546fd9 and reviewer probe 261121-143950-b0ce0e (both --fails-without at 40b1113); suite 261121-143850-ef4b0f |
| F2 | consensus:7f1f7e1: `CHECK(co_await reaches(2))` between the Begin's `put` and the Accepted's `receive`. | bulk-taken mutant 261121-143905-8c1df5 (now a CHECK at :372, not the 60 s timeout) |
| F3-F6 | Not changed: F3 kept as a guard, F4 kept as traced, F5 and F6 need nothing here. | none |

## Result

`PeerOptions::wake` (consensus include/consensus/transport/peers.h), contract in
docs/mechanisms/peers-wake.md, tests test/peers_wake_test.cc (ctest
consensus_transport_peers_wake_{idle,control,bulk}), mutants test/peers_wake_mutants.py.
Leaves open: Detent's multi-voter-idle-wake wires it to OwnerWake and pins past it.

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
- R7: the control case checks `stats.admitted == 4` (each lane authenticated once), not
  reconnects/rejected: voter 3's handshake bound (2) turns away one of the four simultaneous
  dials, which counts a reconnect and a rejection on voter 2 but never opens a session.
- R8 (F1): the refused-retry step closes the Worker rather than making `process` throw on a
  retry: a held Begin can only be retried Busy until something changes, and a closing Worker
  is the reachable change that refuses it with nothing consumed while the session is still
  open. It covers both paths the review named (held frame, closing Worker) in one step. It is
  the bulk case's last step because the closed Worker refuses every later session.
