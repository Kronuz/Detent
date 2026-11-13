# wheel-followups: journal

State: working
Quest: .campaign/detent/quests/wheel-followups.md (the lead's; never edit it)

## Doing

A1-A3 and the A6 fix are committed (wheel:b82d9a6, wheel:36e9c20); docs for A4/A5/A6 being written
at wheel:HEAD of quest/detent/wheel-followups. Jobs queued for every criterion (see Evidence).

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
3. test/summary_gate_test.py is run as a job (`python3 test/summary_gate_test.py`): CMakeLists.txt
   is outside the file set, so it is not a CTest. Lead: add `add_test` if CI should run it.
