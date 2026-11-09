# retirement-close-race: journal

State: waiting on jobs 261109-091516-8a1684 091517-bb33d3 091518-c83b2f 091519-0f30d7 091521-43baf8 091522-70963d 091523-b003a4 091524-b72f0a 091524-3d7822
Quest: .campaign/detent/quests/retirement-close-race.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on the jobs in State, all at Detent:5772d20 (code: seams+test 10b4b13, fix d5d9bc9, driver 5772d20).

## Next

1. When the jobs finish: fill the Evidence table, set State to ready for review.

## Blocked on

Nothing.

## Evidence

All jobs `261109-`; driver `docs/mechanisms/retirement-wakeup.sh`.

| Criterion | Evidence |
|---|---|
| A1 | 085248-0c7d20 (namespace-s1-s2's probe at main's blob dd31703, exit 1); 091516-8a1684 (same probe, 300k rounds, against base 018202d, expect exit 1) |
| A2 | positive 091517-bb33d3 (head); revert 091518-c83b2f (head's test against 10b4b13's retirement.cc, the fix's parent; expect exit 1, failing close and rollback at `worker_admitted`); mutants (pass iff killed) reread-admission 091519-0f30d7, swap-rollbacks-admission 091521-43baf8, drop-closing 091522-70963d |
| A3 | 091523-b003a4 (probe, 300k rounds, -O0 and -O2, at head) |
| A4 | tsan gate 091524-3d7822 |
| A5 | native gate 091524-b72f0a |

## Result

`RetirementState::step` loads `rolling_back` and `closing`, then `admission` once, and decides drainage and pending from those values (`src/detent_distribution/retirement.cc`, Detent:d5d9bc9); `run()` no longer rereads. The same change closes the sibling window for a rollback (a failed `make` on a closing domain): `rolling_back` returns to zero after its admission decrement, and main reread it after loading admission. Seam: `detail::RetirementTesting::interleave` with four `RetirementPoint`s (Detent:10b4b13). Test: `drains_across` in `tests/detent_distribution/test_retirement.cc`, four forced interleavings, no sleeps (a 5 s deadline only detects the hang). How it works: `docs/mechanisms/retirement-wakeup.md`.

## Rulings I made

1. Fix on the worker side, not by setting the closed bit before the wake in `close`: once the bit is set an empty domain can drain and the application may destroy the runtime, so `close` must not touch `runtime->wake` after it (the same rule `release` and `undo_resident` follow).
2. Rollback window fixed in the same change (same file, same mechanism, same strand after close), tested by the rollback cases.
3. Seam points sit just before and just after the admission load, so any marker load moved after it, or any second admission load, falls inside a parked window. `closing` loaded after `admission` is an equivalent mutant (`closing` only rises, and a consumed wake makes it visible), so it has no test.
4. The worker parks on the second pass that sees the wake: the first may have begun before consuming it.
5. Rejected: folding `closing` and the rollback count into the admission word (one load, no ordering rule). It narrows the accepted `owners` range, a user-visible change, for no measured gain.
6. Probe copied into `docs/mechanisms/retirement-wakeup-probe.cc` (from namespace-s1-s2's `docs/measurements/sync-groups-20261108/retirement_wakeup.cc`) so A1/A3 rerun from this branch; the driver builds with clang++ directly (the native gate covers CMake).
7. --minutes: driver test/mutant jobs about 30 s locally, bound 5; probe 300k rounds about 30 s per build at main's rate, bound 15 (10 for A1, which stops at the first stall); gates as configured.
