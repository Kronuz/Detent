# wheel-followups: journal

State: waiting on job 261112-163951-fbebd9 (A5 rerun at head); then ready for review
Quest: .campaign/detent/quests/wheel-followups.md (the lead's; never edit it)

## Doing

A1-A3 and the A6 fix are committed (wheel:b82d9a6, wheel:36e9c20); docs for A4/A5/A6 being written
at wheel:HEAD of quest/detent/wheel-followups. Jobs queued for every criterion (see Evidence).

## Findings so far (local probes, to be replaced by the jobs' numbers)

- A5: max_late equals the owner's longest loop iteration (one expire that drains a backlog of
  about 0.5-0.6M entries in 4-5 ms, ~9 ns each) and occurs after the producers finished; with
  producers paced to stash's rate (100-300 ns an arm) lateness is 0.14-0.30 ms. Cause: a burst of
  arms at 290M/s (P = 4) outruns the owner's filing (~110M/s); inherent to the inbox, so
  documented, not fixed. Jobs: 261112-160248-bd22ed (late mode).
- A6: cause is expire() walking up to 4,096 slots bit by bit per wake after a far jump; fixed by
  walking bitmap words (wheel:36e9c20): 2.03 -> 0.18 us a wake at N = 1e3 (locally). Paired:
  261112-160243-d839de (wake), no regression: 261112-160245-27a1ef (single).

## Note

The first A1 mutant (261112-160913-754337) survived: the second entry's fire re-entered expire and
asserted by itself. Fixed in the test (plain fire for the second entry), mutant resubmitted.

## Next

1. Read the A5 (late) and A6 (ab wake, ab single) job logs; fill LATENESS_CAUSE and SPARSE_WAKES
   in docs/timer-wheel.md with their numbers.
2. Resubmit evidence jobs at the final head (`campaign verify wheel-followups --for A1,A2,A3,A6`).
3. Paste `campaign brief`'s Evidence table; State "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261112-161404-c64d69, 261112-161409-113782; fails without: 261112-161358-e2692f |
| A2 | 261112-161404-c64d69; fails without: 261112-160916-710092 |
| A3 | 261112-161404-c64d69 |
| A4 | `README.md`: "Performance" gives the library's real numbers (arm, whole life, cross-thread arm, bytes), stash only on apples-to-apples rows; `docs/timer-wheel.md`: "Cost" likewise; no prototype numbers |
| A5 | 261112-163951-fbebd9 (rerun at head of 261112-160910-a7d660; cause documented in "Fire lateness") |
| A6 | 261112-161404-c64d69, 261112-161409-113782; fails without: 261112-160216-3b15c9, 261112-160839-ef94c0, 261112-160920-572262; paired 261112-160906-c85f23, 261112-160908-f37dd5 |

## Result

A5: the lateness is the owner's longest loop iteration, one expire draining a 0.3-0.9M backlog
(4-5 ms) after the producers finish; unpaced worst fire 0.21 / 3.48 / 4.08 ms at P = 1 / 4 / 14,
0.2-0.9 ms with producers paced to stash's rate; inherent to the inbox, documented
(wheel:cfdbbe6, docs/timer-wheel.md "Fire lateness"). A6: expire walks bitmap words (wheel:36e9c20);
wake 201.6 ns against 264.4 ns old, ratio 0.773 (0.724-0.801), A/A 3.1%; dense row unchanged
(0.988, A/A 3.4%). Stash vs wheel at small N was not rerun. A4: README "Performance" and
"Cost" state the library's numbers.

## Rulings I made

1. A4 also cleans the prototype / candidate-letter mentions D83 forbids in ARCHITECTURE.md,
   AGENTS.md and CHANGELOG.md (each a line or a clause), though outside the quest's file set: the
   decision says README or docs, and those are docs.
2. N4 is a Debug assert reusing `expiring_` (set in the destructor around the dying fires), so
   the existing re-entrancy assert covers it at no release-build cost.
3. The lead's msg (file set grown): summary_gate_test.py is CTest wheel_summary_gate; `sparse` counts
   the bitmap words expire() visits (WHEEL_TEST_SCANNED, empty outside tests; at most 65 a wake, a
   bit-by-bit walk visits 4,096), no wall-clock in the suite; timing stays in the A6 exclusive A/B
   (261112-160906-c85f23, -160908-f37dd5). A5 is documented as burst arrival vs the owner's filing.

   Next after jobs: fill LATENESS_CAUSE and SPARSE_WAKES in docs/timer-wheel.md from 261112-160910-a7d660
   and -160906-c85f23, rerun only what changed, paste the Evidence table.
