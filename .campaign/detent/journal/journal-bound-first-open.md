# journal-bound-first-open: journal

State: blocked on owner: the fix costs +78% open time (733/743 to 1,332/1,292 ms), inherent to a host.h-only fix
Quest: .campaign/detent/quests/journal-bound-first-open.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 (coordinator's message: lost_bound fails at 529565e; open time +80%; sweep the class).
Local runs under `quiet light` (scratch `.scratch/detent/journal-bound-first-open/`: lbt.sh,
profile.sh, probe_main.sh; outputs attribution.txt, fixed.txt, zeroed.txt, probe_main.txt,
profile.txt). Head journal:4d3df89, not pushed; no evidence queued (waits on the ruling below).

1. Cause of "gap, later turn: the uncut open bounds group 1" (lost both): the merge, not the fix.
   journal:0951710 (pre-merge) 138 checks, 0 failures; journal:179e1c6 (the merge) 1 failure;
   mutant journal:b050603 (no fix) and journal:529565e (fix) the same 1 failure. RFTW0002 checks
   whole sectors: gap() put batch 3, and its flipped byte, in W+1's first sector, which holds W+1's
   header and with it W's manifest (rule 3: each block's manifest is in the next block's header),
   so no manifest named W and every group was lost. Fixed in the layout, not the check
   (journal:00d25fe): batch 3 sits in W+1's second sector; first_open asserts both groups' status
   and bound per layout. `spans()` on main has the same flaw (it passed vacuously: "lost" is
   accepted); it now uses gap() and lays its damage off the header sector too.
2. Class sweep, and what it found: with the layouts fixed, main's host.h (mutant journal:f9df208)
   agrees at every cut of all four flipped-byte layouts (154 checks, 0 failures), and so does the
   reviewer's probe at main's host.h (seeds 2,028 checks, nested 6,208, 0 failures).
   journal-turn-sector-checks made a failed sector past an earlier epoch's kept turn damage
   (turn_log.h `recover`, the `else` of `if (newest_epoch)`), so a flipped byte survives the
   first open's cut without the fix. A lost write does not: a zeroed sector reads as never
   written, and only rule 6 (a later turn, newest epoch alone) shows it. journal:0dd28f0 adds
   "dropped turns, zeroed" and "gap, later turn, zeroed": with the fix 344 checks, 0 failures
   (journal:0dd28f0); without it (mutant journal:c76f1f4) "gap, later turn, zeroed" disagrees at
   11 of 17 cuts (cuts 6-16, both reopens: groups reopen stored at next 2, uncut bounded at 3),
   the original N1 count. So the fix is still needed, for lost writes; A2's fails-without is this.
   "dropped turns, zeroed" agrees at main (an incomplete turn followed by a complete one is
   damage in any epoch): a guard.
3. Open time: job 261124-063448-c99934 (exclusive, 192 MiB, 7 runs x 2): main 4eb8165 732.6 and
   743.2 ms, fix 1,332.3 and 1,292.4 ms, +78%. Not the merge (main itself is at RFTW0002).
   `sample` of 30 opens (profile.txt, sample-*.txt): at the fix, Host::Host 11,887 samples,
   settle_damage 4,537 (3,811 TurnLog::read: the full recovery scan, preads, record assembly;
   726 freeing the returned map of every record), the drivers' own TurnLog recovery 4,579. The
   extra cost is exactly one more recovery. Inherent to a host.h-only fix: `TurnLog::read` is the
   only way host.h can see the verdict before the TurnLog constructor writes the open record.
   docs/mechanisms/lost-bound.md (journal:4d3df89) states the cost and why the verdict precedes
   the open record under RFTW0002.

Earlier round (superseded): jobs 261124-063445-61ecfe (A1, failed: cause above),
261124-063446-3123df (A2 at mutant b050603: exit 1, but only the uncut check failed, not the
cuts: no evidence), CI run 45415700103 (failed, same cause).

## Next

1. Owner's ruling (Blocked on). Then the final head; queue the evidence once there:
   lost_bound (A1), `--fails-without` at the no-fix mutant (A2, must fail at "gap, later turn,
   zeroed"), the open-time A/B (exclusive), `campaign verify` (suite, CI); push.
2. The durability review must re-check: the test changed (layouts, zeroed variants) and, if the
   ruling moves the fix into turn_log.h, the fix's code.

## Blocked on

Owner (via the lead): the fix makes every open, damaged or not, read each log twice: +78% open
time on a 192 MiB log (733/743 to 1,332/1,292 ms, c99934), for a case only lost-write damage past
the newest epoch's kept turn now needs (RFTW0002 already keeps flipped-byte damage). Options:
1. Land as is, the cost recorded; journal-open-single-read removes it later.
2. Widen this quest's files to turn_log.h and turn_driver.h and store the verdict between
   `TurnLog::recover` and the open record (one read; the code changes, so a re-review).
3. Merge this quest into journal-open-single-read.
Recommendation: 2 (or 3). A +78% open regression for every open to cover lost writes is the
wrong trade when the one-read hook removes it; the test (zeroed layouts, A2 mutant) carries over.

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
