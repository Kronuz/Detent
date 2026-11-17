# consensus-driver: async-drivers 4: consensus Driver on task, Sink output, witness, blocking entry

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-driver`.

## Outcome

consensus runs on task through a `Driver` over the I/O-free `Core`, per `docs/design/async-drivers.md` (Detent) Build plan row 4: every D2 call (`propose`, `committed`, `change_membership`, `transfer_leadership`, `repair`, `checkpoint`, install activation) is a task call with its D8 row; output goes to a host-supplied Sink (Q2 (a)); a lone voter campaigns at `Start`; `consensus::run` is the blocking entry (D5); a witness member runs through `Driver` (Xapiand cluster-release).

## Acceptance criteria

- A1 `simulation_test` runs through `Driver` on `inline_scheduler` plus `HeldCompletion`; every D2 call completes per its D8 row (`propose` at ProposalPlaced, `committed` at commit or `superseded`), and Detent's and the Worker suites' uses of `try_submit` are expressible without it: tests
- A2 A lone voter with no witness campaigns at `Start`, so its first proposal after `create` and after `recover` succeeds with no clock advance (X5, R9): `lone_voter` and a recover test
- A3 The waiter protocol per interleaving, close with linked waiters of every kind, a cancel racing close and teardown (`waiter_test` and its close/teardown cases, the resume-before-unlink mutants killed) under ASan, and close racing cancel and teardown on real threads under TSan: tests and mutant jobs
- A4 `consensus::run` follows D5's five steps (`stalled` on a lost wake with a heartbeat armed, `needs_peer` for a group with another voting member, two voters beside a witness included (a lone voter beside a witness is not a valid membership, membership.h D9; journal R8), staged-then-cancelled written before return; cross-thread cancel returns `cancelled`) under TSan; a witness member votes, never applies or holds state (simulation test): tests
- A5 Idle cost per group: 0 threads, frames, armed timers and descriptors, and a 3-voter member's consensus state within 8 KiB on the Linux gate host (Y9); Worker suites green; ASan+UBSan and TSan: an idle-cost job and the CI

## Contracts

Provides `consensus::Driver`, the Sink interface, `consensus::run`. Consumes journal-driver and task/completion. Today's Worker and Peers stay until worker-pump-removal.

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id.

## May land early

Tests; Driver is additive beside the Worker.

Note (journal-driver R14): journal drops `migrate`, `layout` and `finish_migration`; consensus `worker.h:167-181` must drop those calls when it repins journal.
