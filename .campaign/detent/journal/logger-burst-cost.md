# logger-burst-cost: journal

State: ready for review
Quest: .campaign/detent/quests/logger-burst-cost.md (the lead's; never edit it)

## Doing

Nothing: only jobs remain at logger:7e261c1: suite 261116-012408-fce080, A/B
261116-014628-44ad05, A/A 261116-014632-1037e9 (exclusive, 30 rounds, binaries from
261116-012417-ec9d5f in ~/.scratch/logger-burst-cost/final).

## Next

1. Read 44ad05 against the 1.10x rule (D77) and 1037e9 for the band; paste the rows into Evidence
   and Result.
2. CI (A3's "ci" check) needs the branch pushed: the lead's.

## Blocked on

Nothing.

## Mechanism (A1)

Bisect (261116-001349-19ec7c, exclusive, 20 interleaved rounds, 7 arms): the whole step is at
a07f3fe (burst 1.446 / 1.847 at 1 / 4 producers); b121f2e, 3bdf664, 4b5b847, 907d27f (workpool)
are flat against it. The fix arm (logger:d83145c) is 0.980 / 1.043. Rows: logger
`docs/perf/burst-cost.md`.

a07f3fe ported arm D's change 3 (3dcc62e): `wake_for` woke the sleeping LOG thread when a pushed
line was due within kWakeSlackNs = 8 ms of its wake (`due - kWakeSlackNs < worker_wake`). The
slack is for deferred lines (filed in the wheel before due) but applied to routine ring lines too
(`ring_published`): in a burst, a batched line is due stamp + 1 ms, just past the wake the LOG
thread set from the previous due, so every line woke it again: a mutex + notify on the producer
per line, and a LOG thread passing over the producer's ring with nothing due. At f61d8df the test
was `due < worker_wake`, so batched lines never woke it.

Fix (logger:d83145c): the slack only for deferred task records (`push_task` with `!ordered`);
routine lines and queue-order records wake on `due < worker_wake` again.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261116-001349-19ec7c (bisect, exclusive, 7 arms x 20 rounds): a07f3fe 1.446 / 1.847 burst (1 / 4 producers), later commits flat; mechanism above and logger docs/perf/burst-cost.md |
| A2 | pending 261116-014628-44ad05 (A/B) and 261116-014632-1037e9 (A/A); screen in 19ec7c: fix arm 0.965-1.043 on all six rows. Fix test: 261116-012402-971dec (fixed tree 1 notify over 1,000 batched lines; mutant 794, killed) |
| A3 | 261116-012411-484c4f (deferred mutants early, late, ties killed); suite pending 261116-012408-fce080 (runs transcript golden, deferred, deferred_ties, wake); ci needs a push (lead) |
| A4 | logger `CHANGELOG.md` "2026-11-16 (a producer's burst cost)" (logger:35ad331, wording at 7e261c1) |

Test history: 261116-001415-0e454e (ru_nvcsw is 0 on macOS: mutant survived);
261116-005551-2b2823 (context switches: fixed 114 vs mutant 239 under load 49/16);
261116-012205-11c16c (BSD syscalls: fixed 685 vs mutant 1,664, the LOG thread's own timed waits
dominate). The test now reads `logger_detail::worker_notifies`.

## Result

logger:d83145c restricts the 8 ms wake slack to deferred lines; routine and queue-order lines wake
the LOG thread only when due before its wake, as at f61d8df. Screen (19ec7c): burst 0.980 / 1.043,
back to back 0.965 / 0.979, end to end 0.980 / 1.008 against f61d8df. Deferred lines' wheel,
filing rules and lateness unchanged. Docs: logger docs/perf/burst-cost.md, ARCHITECTURE.md
(filing rule 2), CHANGELOG.md. Open: a burst still costs the LOG thread one timed wait per due
line (685 BSD syscalls over 1,000 lines on the fix); not measured at f61d8df, so not attributed;
a LOG-side cost, not the producer's.

## Rulings I made

- Tools in logger's bench/: `logger_ab.py` (Detent:8d7561a3's driver, generalized to several
  `--after` arms interleaved per round in rotated order, so the bisect is one exclusive job) and
  `ab_build.sh` (builds logger_bench per tree; refuses trees whose bench/lg.cc differs). lg.cc is
  identical across f61d8df..907d27f.
- Bisect arms are the commits that change the library (logger.cc, logger.h, CMakeLists.txt):
  a07f3fe, b121f2e, 3bdf664, 4b5b847, 907d27f; the rest touch only bench/docs/tests.
- The fix's test reads `logger_detail::worker_notifies`, a relaxed counter bumped only on
  wake_for's notify path (already a mutex + syscall): process-wide counts (context switches,
  syscalls) could not separate a producer's notify from load or the LOG thread's own waits. It is
  in logger_detail beside worker_wake, not public API; `sizeof(Logging)` is unchanged.
- ARCHITECTURE.md's second filing rule now names deferred lines only.
