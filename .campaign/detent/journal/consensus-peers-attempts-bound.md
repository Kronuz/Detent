# consensus-peers-attempts-bound: journal

State: ready for review (land under D116 after consensus-worker-release)
Quest: .campaign/detent/quests/consensus-peers-attempts-bound.md (the lead's; never edit it)

## Doing

Done at consensus:12db7c6; nothing left but landing.

## Next

1. Lead: merge consensus main, land under D116.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | test/transport/peers_test.cc outage: comment cites 0bb37f, 2b4b98; the failing side is too few (CI log: 2 lookups in 1341 ms); the stall is inferred, not reproduced (402 runs, none below 4) |
| A2 | af2cb5 (200 loaded runs pass), f6309f (--fails-without, no-lane-pause mutant 623f91f: 200 of 200 runs fail the rounds bound at :627) |
| A3 | f15d0c (suite); CI at 12db7c6 failed only macos-x86_64 groups_upload, D116's standing exception |

## Result

peers_test outage no longer times the fourth lookup in a 1 s window: it awaits it (5 s), and bounds
lookups by resolve_deadline spacing (elapsed/200+1) and reconnect rounds by the lane pause
(2*(window/100+1)). Probes 0bb37f, 2b4b98: 5 lookups in 1.24 s (4 in 2 of 404 runs), gaps 200-358 ms.
The CI stall did not reproduce; it is inferred (the macOS x86_64 VM stops the process), and said so
at the bound. The old upper bound (26) could not see a removed pause; the rounds bound does.

## Rulings I made

1. Awaited the lower bound instead of widening the window: any timed count fails a stopped process.
2. Upper bound on rounds, not lookups: lookups are held 200 ms apart by resolve_deadline regardless of the pause.
