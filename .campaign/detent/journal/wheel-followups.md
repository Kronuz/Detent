# wheel-followups: journal

State: waiting on jobs 261112-160933-266ff2 261112-160839-ef94c0 261112-160848-be2b8c 261112-160906-c85f23 261112-160908-f37dd5 261112-160910-a7d660 261112-160913-754337 261112-160916-710092 261112-160920-572262 261112-160923-9ecbc0 261112-160926-aab121
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
| A1 | pending |
| A2 | pending |
| A3 | pending |
| A4 | pending |
| A5 | pending |
| A6 | pending |

## Result

pending

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
