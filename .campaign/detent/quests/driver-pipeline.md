# driver-pipeline: Consensus: driver groups pipeline proposals

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status driver-pipeline`.

## Outcome

Xapiand's consensus-flow-bench (T7) found two caps on cluster write throughput: the Worker builds every Core with `Flow{}` (worker.h:144, 172, 173, 646) and `GroupOptions` has no Flow, so every driver group runs at pipeline 1; and Core persists one proposal at a time and rejects Propose while busy, so a group commits at most one command per leader barrier (about 500-1,000 commits/s with a 1-2 ms fdatasync). After this quest a driver group pipelines proposals up to a configured depth, with Core accepting proposals while one persists, and throughput per group scales with depth up to the disk's batch rate, measured with T7's bench.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `GroupOptions` carries the flow settings, and a driver group at depth N has up to N proposals in flight: a test.
- A2 Safety holds at depth > 1: the simulation's linearizability and log-matching oracles pass with 2-4 loops per host at depth 1, 4 and 16 over 10,000 seeds, plus TSan: jobs.
- A3 Commits per second per group at depth 1, 4 and 16 on Linux (the VM, real fdatasync) and macOS, before and after, exclusive for the deciding numbers: jobs and `docs/perf/`.
- A4 The suite passes, plus CI.

## Contracts

consensus GroupOptions gains flow settings; default depth stays 1 unless the numbers say otherwise (a lead decision on the record).

## Decisions

Filed by the lead 2026-11-18 at the Xapiand lead's request (T7). Starts when T7's numbers show the gap and worker.h and driver.h are free.

## May land early

None.
