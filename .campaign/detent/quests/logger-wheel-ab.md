# logger-wheel-ab: Measure logger on scheduler vs on wheel

Risk: low (measurement; logger's API and behavior do not change). Repo `../logger`. The lead
writes this quest; contributors read it and never edit it. Machine state: `campaign status
logger-wheel-ab`.

## Outcome

`docs/perf/wheel-ab.md` in Kronuz/logger answers, per pattern, whether logger's scheduling
costs less on Kronuz/wheel than on Kronuz/scheduler (stash), by a rule committed before the
first measured run (D72 condition 2). The harness in `bench/wheel_ab/` runs both backends on
the same logger workload: (A) logger as it is, scheduler at its current pin; (B) a prototype
backend behind the same three operations on `wheel::timer_wheel` pinned at
`Kronuz/wheel@50665c8` (the wheel-lib branch; rerun at the landed commit if it moves), owned by
the LOG thread, fed by `arm_remote`.

## The three patterns

1. **Immediate**: any thread logs a line that the LOG thread writes now.
2. **Deferred, fires**: a line armed 100 ms to 1 s out that is never cleared.
3. **Slow-op, usually cleared**: a line armed 200 ms out and cleared by the arming worker
   when the operation finishes, at a clear ratio of 99%. On (B) a worker cannot cancel; the
   prototype may clear by a flag the fire checks, or by a remote-cancel path it adds
   (state which; a new wheel API is a proposal for the wheel-lib quest, not a change here).

Each at P = 1, 4 and 14 producer threads, the line formatting stubbed out (a fixed short
message to a null sink) so the scheduling cost is what is measured; also one row with real
formatting to show the share scheduling has in a whole log call.

## Acceptance criteria

- A1 [artifact] The rule, committed before any measured run: metrics (producer ns per log call,
  median and p99; heap bytes per line; fire lag p99; LOG-thread CPU per line), and a pattern
  is a wheel win at a P when (B)'s median is at least 10% below (A)'s beyond the A/A band and
  its p99 and heap are no worse than 1.10 times (A)'s: `docs/perf/wheel-ab.md`: "Rule".
- A2 Every row from an `exclusive` job at a named commit, six interleaved A/B pairs per row
  plus one A/A pair, macOS; correctness first: every line written exactly once, none early,
  cleared lines never written, under TSan for (B).
- A3 [artifact] The verdict per pattern and P, with the numbers, the A/A bands, and what (B)
  needed that the wheel lacks: `docs/perf/wheel-ab.md`: "Verdict".

## Contracts

Consumes `wheel:timer_wheel` (arm, cancel, arm_remote, expire, next). Provides nothing.

## Decisions

D72 (proven better on every current use), D73. A pattern the wheel loses keeps logger on
scheduler for that pattern, or sends a wheel change (a remote cancel) to wheel-lib and a
rerun: the lead decides which with the numbers.

## May land early

The rule (A1) and the harness without results.
