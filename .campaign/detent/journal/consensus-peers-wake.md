# consensus-peers-wake: journal

State: waiting on jobs 261121-140831-f20fe2 261121-140921-85a62f 261121-140841-25783f 261121-140844-0c16f8 261121-140848-6433dd 261121-140852-1056ff 261121-140855-706a5f 261121-140858-daa2fb 261121-140901-80707b 261121-140904-b8f618 261121-140907-8c4cb2 261121-140924-7f1c2c
Quest: .campaign/detent/quests/consensus-peers-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs at consensus:a1855ac, all 12 still queued after one 600 s wait; CI run 44955269607 queued. Locally (dev build, not evidence) idle, control
and bulk passed 3 of 3 each.

## Next

1. Record the jobs below; a red one gets its cause fixed and a rerun.
2. CI green at the head (A5), then State: ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261121-140831-f20fe2 (3 repeats), 261121-140921-85a62f (plain) |
| A2 | --fails-without at refs/mutants/consensus-peers-wake/<name>: control 261121-140841-25783f, bulk-taken 261121-140844-0c16f8, bulk-refused 261121-140848-6433dd, consumed 261121-140852-1056ff, opened 261121-140855-706a5f, lost 261121-140858-daa2fb, timer 261121-140901-80707b, any-lane-opened 261121-140904-b8f618, stop-wakes 261121-140907-8c4cb2 |
| A3 | 261121-140924-7f1c2c (macOS llvm TSan, peers and peers_wake ctest entries) |
| A4 | docs/mechanisms/peers-wake.md (consensus:3576fb5) |
| A5 | campaign verify (CI) at consensus:a1855ac |

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
