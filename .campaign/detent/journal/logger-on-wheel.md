# logger-on-wheel: journal

State: ready for review
Quest: .campaign/detent/quests/logger-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done; A3 rows written up in logger:67391e6 (docs/perf/wheel-port.md, CHANGELOG range).

## Next

1. Correctness review; CI needs the branch pushed (lead).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261112-140315-039e6c (passed at be2106cd8e43; ignores docs-only CHANGELOG.md, docs/perf/wheel-port.sh); fails without: 261112-140328-3415e1 |
| A2 | 261112-140332-797330 (passed at be2106cd8e43; ignores docs-only CHANGELOG.md, docs/perf/wheel-port.sh) |
| A3 | 261112-140421-86b4c3 (passed at 11644a54c61b; ignores docs-only CHANGELOG.md), 261112-140412-ee682f (passed at 11644a54c61b; ignores docs-only CHANGELOG.md), 261112-140415-a16bfe (passed at 11644a54c61b; ignores docs-only CHANGELOG.md), 261112-140418-0c17c8 (passed at 11644a54c61b; ignores docs-only CHANGELOG.md) |
| A4 | 261112-140334-59109a (passed at be2106cd8e43; ignores docs-only CHANGELOG.md, docs/perf/wheel-port.sh) |
| A5 | `ARCHITECTURE.md`: "Deferred lines ride the LOG thread's wheel" |

## Result

A3: no row is past 1.10. All four jobs exit 0 at logger:11644a54c61b, exclusive, 40 rounds a row,
`summarize.py --paired`. Label swap: A and A2 are arm D (`logger_wheel_ab_d`, wheel 45f98eb), D is the
production logger (`logger_wheel_ab`, wheel bfd72bc), so r = D/A is production / arm D; w is the A2/A
band. Full tables in logger `docs/perf/wheel-port.md`.

| row | job | r median [interval] (w) | r p99 [interval] (w) | heap A / D |
|---|---|---|---|---|
| p3 P=1 | 261112-140421-86b4c3 | 0.997 [0.928-1.019] (0.089) | 1.012 [0.875-1.165] (0.176) | 267.7 / 267.7 |
| p3 P=4 | 261112-140421-86b4c3 | 1.010 [0.973-1.041] (0.039) | 1.035 [0.903-1.111] (0.104) | 267.7 / 267.7 |
| p3 P=14 | 261112-140421-86b4c3 | 0.992 [0.982-1.017] (0.029) | 0.979 [0.908-1.100] (0.063) | 267.7 / 267.7 |
| p2 P=1 | 261112-140412-ee682f | 1.009 [0.838-1.196] (0.171) | 1.002 [0.859-1.200] (0.190) | 267.2 / 267.2 |
| p2 P=4 | 261112-140415-a16bfe | 1.013 [0.966-1.044] (0.053) | 0.990 [0.910-1.065] (0.086) | 267.2 / 267.2 |
| p2 P=14 | 261112-140418-0c17c8 | 0.987 [0.954-1.005] (0.029) | 1.036 [0.897-1.173] (0.141) | 267.2 / 267.2 |

Worst point estimates: p99 p2 P=14 1.036, median p2 P=4 1.013. Five rows' intervals reach past 1.10
(reported as straddling, D77 does not gate on it); the A/A twin's p99 intervals are as wide at
P=1 and in pattern 2. Lag p99 and LOG CPU r are within noise of arm D on every row (lag 1.000 to
1.045; LOG CPU 0.978 to 1.158, interval 0.919-1.478 at p2 P=1, A/A 0.734-1.403).

## Rulings I made

- The queues' merge heap (`heads`, rebuilt each pass, holds nothing between passes) is not a
  deadline structure; A4's grep allows `_heap` calls only on `heads`.
- logger.h includes threadpool's `thread.hh` (what it uses) instead of `scheduler.h`; the scheduler
  pin stays because it brings threadpool and test/frozen uses the scheduler.
- The wheel_ab option no longer declares its own wheel pin (50665c8): the library's bfd72bc serves it.
- A3 harness: bench/** is outside the file set, so docs/perf/wheel-port.sh runs bench/wheel_ab's
  binaries (d/CMakeLists build) with row-d's labels swapped (A, A2 = arm D; D = library), so
  summarize.py --paired's r is library / arm D; exits 1 unless every row is "no worse".
- A3 jobs: pattern 3 is one job (P=1,4,14); pattern 2 is one job per P, because a pattern 2 row
  takes ~275 s (Arm D, fixed) and an exclusive job is capped at 10 min (--long jobs prove nothing
  before landing).
- Lateness bound in test/deferred.cc: median <= 1 tick + measured timed-wake median + 0.5 ms, p99
  <= 1 tick + timed-wake p99 + 2 ms; skipped under TSan (never-early, once, order still checked).
