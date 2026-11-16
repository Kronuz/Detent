# logger-burst-cost: journal

State: waiting on jobs 261116-001344-f6f08d 261116-001349-19ec7c 261116-001412-43a3ee 261116-001415-0e454e
Quest: .campaign/detent/quests/logger-burst-cost.md (the lead's; never edit it)

## Doing

Bisect + fix screen queued (build f6f08d, exclusive 7-arm A/B 19ec7c), suite 43a3ee, wake mutant 0e454e.

## Next

1. Read the bisect: expect a07f3fe to carry the burst ratio, b121f2e..907d27f flat, fix arm ~1.0.
2. Submit deferred mutants (`sh test/deferred_mutant.sh`, --for A3 --fails-without), final A/B
   fix vs f61d8df 30 rounds and A/A (base vs base) 30 rounds, both exclusive, `--for A2`.
3. CHANGELOG "burst" entry (A4); docs/perf/burst-cost.md with the rows.

## Blocked on

Nothing.

## Mechanism (A1, hypothesis under test)

a07f3fe ported arm D's change 3 (3dcc62e): `wake_for` wakes the sleeping LOG thread when a pushed
line is due within kWakeSlackNs = 8 ms of its wake (`due - kWakeSlackNs < worker_wake`). That
slack is for deferred lines (filed in the wheel before due). It also applied to routine ring
lines (`ring_published`): in a burst, a batched line is due stamp + 1 ms, just past the wake the
LOG thread set from the first line's due, so every line woke it again: a mutex + notify on the
producer per line, and a LOG thread that passes over the producer's ring with nothing due. At
f61d8df the test was `due < worker_wake`, so batched lines never woke it.

Fix (logger:d83145c): the slack only for deferred task records (`push_task` with `!ordered`);
routine lines and queue-order records wake on `due < worker_wake` again.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261116-001349-19ec7c |
| A2 | pending |
| A3 | pending: 261116-001412-43a3ee |
| A4 | pending |

## Result

<pending>

## Rulings I made

- Tools in logger's bench/: `logger_ab.py` (Detent:8d7561a3's driver, generalized to several
  `--after` arms interleaved per round in rotated order, so the bisect is one exclusive job) and
  `ab_build.sh` (builds logger_bench per tree; refuses trees whose bench/lg.cc differs). lg.cc is
  identical across f61d8df..907d27f.
- Bisect arms are the commits that change the library (logger.cc, logger.h, CMakeLists.txt):
  a07f3fe, b121f2e, 3bdf664, 4b5b847, 907d27f; the rest touch only bench/docs/tests.
- The fix's test counts voluntary context switches (getrusage) rather than adding a wake counter
  to the library: no API or layout change.
