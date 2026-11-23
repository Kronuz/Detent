# journal-bound-first-open: journal

State: waiting on jobs 261123-110049-af725e 261123-111045-7f9830 and CI
Quest: .campaign/detent/quests/journal-bound-first-open.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs: A2 261123-110049-af725e (--fails-without at journal:900a11a), open-cost A/B
261123-111045-7f9830 (strict, main e58f977 vs fix journal:870382d), CI on the pushed branch
(journal:0951710; suite `macos-arm64` and `ci`).

## Next

1. Paste `campaign brief`'s Evidence table; open-cost numbers into Result.
2. After journal-turn-sector-checks lands: merge main, update host.h comments naming RFTW0001
   (its journal lists them), re-sync `gap()` in lost_bound_test.cc if its offsets changed (it
   copies `spans()`'s layout), rerun the suite.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <CI suite at head; `lost_bound_test first_open`> |
| A2 | <261123-110049-af725e> |
| A3 | `docs/mechanisms/lost-bound.md`: "the shard, the state byte's `bounded_state` bit, then 8 bytes after the base). The verdict is stored" |

Jobs examined: 261121-141538-9a63b7 and 261121-141627-fe334f (xapiand review probe, 11 of 17
cuts disagree at ebd3038; read first).

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Fix in host.h only, no turn_log.h change: `settle_damage` reads each log with the existing
  `TurnLog::read` and stores the verdict before the drivers' open records. Cost: one more read of
  each log per open (numbers in Result). The alternative, a hook between `TurnLog::recover` and
  the open record (turn_log.h, turn_driver.h; outside my file set), would read once; it is the
  lead's call if the measured cost matters.
- The drivers recover against the table as it was before `settle_damage` (the same
  `TurnLogGroups`), so `TurnDriver::recovered()` reports the same bounds and losses as before the
  fix and the open's existing store/keep pass is unchanged (its commits become no-ops). Recovering
  against the updated table instead made `recovered().bounded` empty, which the `bounds` case
  checks.
- The test is a new case (`first_open`) with a new layout helper `gap()` copying `spans()`'s
  layout; `spans()` is untouched (the lead's heads-up: journal-turn-sector-checks edits its
  offsets). Four layouts: gap at batch 3, gap at both fragments (both lose the groups: the gap's
  last block is the newest, no manifest), gap with a later turn (bounds both at 3), dropped turns
  rule 2 proves (bounds both at 2; agrees at main too, kept as a guard).
