# logger-burst-cost: journal

State: waiting on jobs 261116-012205-11c16c
Quest: .campaign/detent/quests/logger-burst-cost.md (the lead's; never edit it)

## Doing

Wake mutant 11c16c at logger:35ad331 (syscall count instead of context switches) checks the
test's threshold on the fixed tree and the mutant.

## Next

1. If 11c16c passes (fixed < 250, mutant >= 250): submit at the head: suite (`campaign verify
   --for A3`), deferred mutants (`sh test/deferred_mutant.sh`, --for A3 --fails-without), build
   (ab_build base=f61d8df fix=.), then exclusive A/B fix vs base 30 rounds and A/A base vs base
   30 rounds (`--for A2`, --after the build).
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
| A1 | 261116-001349-19ec7c (bisect, exclusive); mechanism above and in logger docs/perf/burst-cost.md |
| A2 | pending (screen in 19ec7c: fix arm 0.965-1.043 on all six rows) |
| A3 | pending |
| A4 | logger `CHANGELOG.md` "2026-11-16 (a producer's burst cost)" (logger:35ad331) |

Test history: 261116-001415-0e454e (ru_nvcsw is 0 on macOS: mutant survived);
261116-005551-2b2823 (context switches: fixed 114, mutant 239, threshold 100 failed the fixed
tree under load 49/16), so the test counts BSD syscalls (task_info) on macOS.

## Result

<pending>

## Rulings I made

- Tools in logger's bench/: `logger_ab.py` (Detent:8d7561a3's driver, generalized to several
  `--after` arms interleaved per round in rotated order, so the bisect is one exclusive job) and
  `ab_build.sh` (builds logger_bench per tree; refuses trees whose bench/lg.cc differs). lg.cc is
  identical across f61d8df..907d27f.
- Bisect arms are the commits that change the library (logger.cc, logger.h, CMakeLists.txt):
  a07f3fe, b121f2e, 3bdf664, 4b5b847, 907d27f; the rest touch only bench/docs/tests.
- The fix's test counts waits and wakes (BSD syscalls via task_info on macOS, voluntary context
  switches on Linux) rather than adding a wake counter to the library: no API or layout change.
- ARCHITECTURE.md's second filing rule now names deferred lines only.
