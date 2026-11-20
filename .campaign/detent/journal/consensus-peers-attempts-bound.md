# consensus-peers-attempts-bound: journal

State: waiting on jobs 261119-212019-0bb37f (probe c67c493 on probe/consensus-peers-attempts-bound)
Quest: .campaign/detent/quests/consensus-peers-attempts-bound.md (the lead's; never edit it)

## Doing

CI run 44650101243 printed "NXDOMAIN at startup: 2 lookups of two in 1341 ms": the too-few side
failed (2 < 4), not the upper bound. Lookups of one name are spaced by resolve_deadline (200 ms) and
by the lane's 100-199 ms pause, so expect ~250 ms apart (5 in 1.3 s); 2 means the executor or
lookup thread stalled ~800 ms. Probe (does not land) prints each lookup's time, the commit time,
elapsed and attempts per run; probe branch probe/consensus-peers-attempts-bound at c67c493.

## Next

1. Read 261119-212019-0bb37f: spacing of lookups, which runs fail, commit time.
2. Name the cause; set or fix the bound at peers_test.cc:621 with the job ids.
3. A2 loaded 200 runs + mutant (no backoff delay), A3 suite and CI.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | |
| A2 | |
| A3 | |

## Result

## Rulings I made
