# wheel-followups: Wheel review follow-ups: N4, R1, R2

Risk: low. Repo `../wheel`. After task-wheel and wheel-vs-stash. The lead writes this quest; contributors read it and
never edit it. Machine state: `campaign status wheel-followups`.

## Outcome

The non-blocking findings of wheel-lib's correctness review (`reviews/wheel-lib-correctness.md`)
are closed, none silently.

## Acceptance criteria

- A1 N4: a dying (`false`) fire that calls `expire()` cannot overwrite the destructor's detached
  list: a Debug assert or a guard, with a test, and one line in `docs/timer-wheel.md`.
- A2 R1: `wheel_bench_summary.py --gate` exits non-zero when it compared nothing (no reference
  arm, no gate arm, or no common rounds); a test feeds it such a log.
- A3 R2: `producer_cpu_ns` is measured (per-thread CPU time on macOS and Linux) or removed with
  its dead `on_cpu`; the summary's zero-metric skip is gone either way, so a metric that reads 0
  stops the summary.

- A4 [artifact] D83: README "Performance" and `docs/timer-wheel.md` "Cost" give the library's own
  numbers from wheel-vs-stash (ns per operation, bytes per entry, cross-thread arm) and stash's
  only where that file calls the row apples to apples; no prototype, candidate letter, or
  ratio to one: `README.md`: "Performance".

- A5 The cross-thread fire lateness wheel-vs-stash found (worst fire 3.4 ms at P = 4 and 4.5 ms at P = 14 against stash's 1.02 ms, plain medians, `docs/perf/wheel-vs-stash.md` in Detent) is explained by its cause in a strict job, and fixed when the cause is the wheel's, with a paired before/after; or the doc names it as the tick plus the owner's wake, with numbers
- A6 The per-wake drain cost at small N (about 2 µs per wake at N = 1e3, the niche row where the wheel costs 2.3x stash at P = 1) is explained by its cause and reduced when the cause is the wheel's, with a paired before/after; or stated with its numbers in `docs/timer-wheel.md` "Cost"

## Contracts

None changed: `wheel:timer_wheel`'s API is untouched.

## Decisions

D77 (the paired statistic stays).

## May land early

Each criterion alone.
