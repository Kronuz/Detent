# journal-bound-first-open: journal

State: waiting on jobs 261124-113916-a045ce 261124-113919-ce832d 261124-113923-f05db6 and the suite/CI checks at journal:a2d65da
Quest: .campaign/detent/quests/journal-bound-first-open.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 (coordinator: file set widened with turn_log.h; the +78% is not accepted). Head
journal:a2d65da, pushed. Evidence queued once: A1 261124-113916-a045ce (lost_bound, every case),
A2 261124-113919-ce832d (`--fails-without`, first_open at mutant journal:b7d73c1 = head with
origin/main's host.h and turn_log.h; ref refs/mutants/journal-bound-first-open/no-fix), open-time
A/B 261124-113923-f05db6 (exclusive, main 4eb8165 vs a2d65da, 192 MiB, 7 runs x 2, same shape as
c99934; must land in main's band, 733/743 ms), `campaign verify --for A1` (suite, CI).

Mechanism change (the durability review must re-check it): no `TurnLog::read`. `TurnLogGroups`
gains `settle`, a callback the `TurnLog` constructor calls with its recovery before it writes the
open record; `Host::open` sets it on each loop's groups to `settle_damage(recovery)`, which commits
the bounds (`mark_bounds`) and the losses (`mark_lost`). Each log is read once. The verdict is
stored per loop before that loop's open record (before: all loops before any). `mark_bounds`'s
second pass (clearing a bound a later checkpoint supersedes) reads every driver's records, so it
runs only once `drivers_.size() == loops_` (before: `!drivers_.empty()`; the same outside the
open), and the open's post-driver pass runs it as on main. turn_driver.h is unchanged: it passes
`known` through.

Local (quiet light, scratch round3.txt): at a2d65da lost_bound 344 checks 0 failures (17, 17, 12,
17, 12, 17 cuts; every layout's both-group verdict as asserted), host 77,730, placement 1,197,906,
turn_log 12,099, host_delete 2,107, driver 49, turn_log_power 31,268, all 0 failures. Mutant
b7d73c1: 22 failures, all "gap, later turn, zeroed" at cuts 6-16 of 17 (11 cuts, both reopens).

Same-layout sweep on main: spans() was the only test laying a two-block gap with its damage in
W+1's first sector (fixed here, in my set). Other loss-tolerant checks (turn_log_test 191, 240,
264; placement_test 192, 282; crc_fixture_test 303) are safety properties over other layouts, and
turn_log_test `damage` asserts a kept group (4) that a lost manifest would fail. The reviewer's
probe (.scratch/detent/review-bound-first-open/probe.cc) includes lost_bound_test.cc's gap(), so it
inherits the fix. Nothing outside my set to name.

Round 2, superseded by round 3: the cause of the 529565e failure was the merge (RFTW0002 checks
whole sectors; gap() damaged W+1's first sector, which holds W's manifest): 0951710 passes,
179e1c6 fails, with or without the fix. Fixed in the layout (journal:00d25fe). RFTW0002 already
keeps a flipped byte's verdict at main (mutant f9df208: 0 failures; reviewer probe at main's
host.h: seeds 2,028 and nested 6,208 checks, 0 failures); lost writes (zeroed sectors) do not
(journal:0dd28f0, 11 of 17 cuts at main). The two-read fix cost +78% open time (c99934: main
732.6/743.2 ms, fix 1,332.3/1,292.4; `sample`: settle_damage's TurnLog::read 3,811 samples plus
726 freeing its records, against the drivers' own recovery 4,579).

## Next

1. When the jobs finish: Evidence from `campaign brief`; Result with the A/B numbers; "ready for
   review" only if A1, A2, the A/B (within main's band), the suite and CI pass. The durability
   review re-checks (recovery code changed).

## Blocked on

Nothing (the open-cost ruling: remove the second read; done in round 3).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <CI suite at head; `lost_bound_test first_open`> |
| A2 | 261124-113919-ce832d (fails without: mutant journal:b7d73c1 fails 11 of 17 cuts) |
| A3 | `docs/mechanisms/lost-bound.md`: "the shard, the state byte's `bounded_state` bit, then 8 bytes after the base). The verdict is stored" |

Jobs examined: 261121-141538-9a63b7 and 261121-141627-fe334f (xapiand review probe, 11 of 17
cuts disagree at ebd3038; read first).

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- With journal-turn-sector-checks landed, `spans()` is mine to change: it now calls `gap(true)`, so
  the layout lives in one place (review F4: a copy drifts). `gap()` value-checks the positions it
  derives against the log's bytes, so a future format change fails there by name, not by a
  missing filler. F2 (dropped() comment, group 2 oracle) and F3 (one commit of bounds and losses)
  stay as the review left them: nits, no measured disagreement; the printed uncut verdicts give
  F2's values once A1 runs.

- Round 3: the hook is a `TurnLogGroups::settle` callback, not a TurnLog constructor parameter,
  so turn_driver.h (outside my set) passes it through unchanged; `TurnLog::read` calls no hook
  (it writes no open record). Superseded: the round-1 two-read fix in host.h alone.
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
