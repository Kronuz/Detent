# wheel-followups: Wheel review follow-ups: N4, R1, R2

Risk: low. Repo `../wheel`. After task-wheel. The lead writes this quest; contributors read it and
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

## Contracts

None changed: `wheel:timer_wheel`'s API is untouched.

## Decisions

D77 (the paired statistic stays).

## May land early

Each criterion alone.
