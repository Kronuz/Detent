# consensus-peers-attempts-bound: journal

State: waiting on jobs 261119-215755-af2cb5 (A2) 261119-215756-f6309f (A2 mutant) 261119-215758-f15d0c (A3 suite), CI at consensus:12db7c6
Quest: .campaign/detent/quests/consensus-peers-attempts-bound.md (the lead's; never edit it)

## Doing

Probes: 0bb37f (quiet, 200 runs) and 2b4b98 (12 workers + 12 spinners, 204 runs): 402 runs, 0 below
4 lookups (5 in 402-ish, 4 in 2 runs; gaps 200-358 ms; 4th lookup at 819-902 ms; watchdog stall
<=27 ms; rounds 12-14 per 1 s window). The CI failure (2 lookups in 1341 ms) did not reproduce, so
the cause is inferred, not seen: a host stall (the file's own note on the macOS x86_64 VM). It
needs the process stopped from before the 3rd lookup (~610 ms) past 1341 ms.
Fix at consensus:12db7c6 (peers_test.cc outage): the 4th lookup is awaited (until, 5 s) instead of
counted in 1 s; upper bounds: lookups <= elapsed/200+1 (resolve_deadline spacing), reconnect rounds
<= 2*(window/100+1) (the lane pause: removed, lookups stay 200 ms apart but rounds explode).
Mutant refs/mutants/consensus-peers-attempts-bound/no-lane-pause (623f91f, pause(0)).

## Next

1. Read af2cb5 (200 pass), f6309f (must fail at the rounds CHECK), f15d0c suite, CI.
2. Fill Evidence and Result; ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | test/transport/peers_test.cc outage: comment cites 0bb37f, 2b4b98; the failing side is too few (CI log: 2 lookups in 1341 ms); the stall is inferred, not reproduced (402 runs, none below 4) |
| A2 | |
| A3 | |

## Result

## Rulings I made
