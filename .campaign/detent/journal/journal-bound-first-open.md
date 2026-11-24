# journal-bound-first-open: journal

State: waiting on jobs 261124-063445-61ecfe 261124-063446-3123df 261124-063448-c99934 and the suite/CI checks at journal:529565e
Quest: .campaign/detent/quests/journal-bound-first-open.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged journal main (journal:4eb8165, RFTW0002) at journal:179e1c6, clean: main's diff touched
none of host.h or lost_bound_test.cc's `gap()`/`first_open`, only `spans()`, `populate`,
`find`/`end_of` (now data positions) and `damage` (now a data position). journal:a7e86dd: host.h:30
names RFTW0002; `spans()` now calls `gap(true)` (one layout, no copy), and `gap()` checks its
positions against the bytes the log wrote (W and W+1 headers with their block index, group 2's
batch 2 a First fragment filling W, its Last fragment opening W+1's data, group 1's batch 3 a
Full fragment right after). journal:529565e prints each first_open layout's cuts and uncut
verdict. Pushed journal:529565e; `campaign verify --for A1` queued the suite and CI.

Jobs: A1 261124-063445-61ecfe (every case at head), A2 261124-063446-3123df (`--fails-without`,
first_open at mutant journal:b050603 = head with origin/main's host.h; ref
refs/mutants/journal-bound-first-open/no-fix), open-cost A/B 261124-063448-c99934 (exclusive,
main 4eb8165 vs head, 192 MiB, 7 runs: the review's F1 asked for exclusive).

After one 600 s wait (2026-11-24 06:4x): A2 built at the mutant (the merged test compiles) and is
running; A1 and the A/B still queued. CI at journal:529565e running.

Coordinator's standing rule (campaign-v3, until evidence-once lands): iterate with `quiet light`
on targeted tests in the worktree; queue evidence once per round, at "ready for review"; never
rerun a passed mutant whose reverted code is unchanged. This round's jobs were queued before the
rule and are its one evidence round. A2 3123df was already running when the rule came: its
reverted code (host.h's fix) is unchanged, but the test it runs moved to RFTW0002 positions, so
it is kept rather than cancelled mid-run; no further A2 reruns unless host.h's fix changes.

Mechanism: unchanged by the merge. host.h's fix (`settle_damage` reads each log with
`TurnLog::read` and commits the verdict before the drivers' open records) is byte-identical to
the reviewed journal:0951710 apart from the comment; the test's layout moved to RFTW0002
positions (review F4) and gained value checks. Re-review is the lead's call.

## Next

1. When the jobs finish: Evidence table from `campaign brief`, cut counts from 61ecfe's log, the
   exclusive open-cost numbers into Result; State "ready for review" only if all pass.

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

- With journal-turn-sector-checks landed, `spans()` is mine to change: it now calls `gap(true)`, so
  the layout lives in one place (review F4: a copy drifts). `gap()` value-checks the positions it
  derives against the log's bytes, so a future format change fails there by name, not by a
  missing filler. F2 (dropped() comment, group 2 oracle) and F3 (one commit of bounds and losses)
  stay as the review left them: nits, no measured disagreement; the printed uncut verdicts give
  F2's values once A1 runs.

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
