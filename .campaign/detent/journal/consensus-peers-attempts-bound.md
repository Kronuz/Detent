# consensus-peers-attempts-bound: journal

State: waiting on jobs 261119-214648-2b4b98 (loaded probe, consensus:<probe head> on probe/consensus-peers-attempts-bound)
Quest: .campaign/detent/quests/consensus-peers-attempts-bound.md (the lead's; never edit it)

## Doing

Probe 261119-212019-0bb37f (consensus:c67c493, 8x25 parallel, quiet host, 200 runs, 0 failures):
every run made 5 lookups of two, at ~1, 259, 608, 860, 1073 ms (gaps 258, 348, 252, 213: the
lane pause is deterministic per round), elapsed 1233-1241, commit 232-240 ms. The 4th lookup lands
at 860 ms, 378 ms before the window ends. The CI failure (2 lookups in 1341 ms) did not reproduce
(0 of 200): the process lost ~700 ms. Lookups are spaced by resolve_deadline (200 ms), not by
"two lanes once per 100 ms", so the old upper bound (26) is 4x loose and cannot see a removed
pause (lookups stay 200 ms apart); the lane pause shows in reconnects rounds. Loaded probe
261119-214648-2b4b98 (12 workers + 12 spinners, adds rounds and a 10 ms watchdog's stall) is
running to see the stall.

## Next

1. Read 261119-214648-2b4b98: does a stall reproduce, and are rounds a usable backoff check?
2. Set the bound/check from the cause with a two-line comment citing the probe ids.
3. A2 loaded 200 runs + mutant (--fails-without); A3 suite, push for CI.

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
