# first-read-1ms: Fresh resident first reads reach p99 below 1 ms on macOS

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status first-read-1ms`.

## Outcome

Fresh resident first reads (1 and 1,000 namespaces, publisher stopped and running) reach p99 below 1 ms on this host; today main is 1.18-1.32 ms and reader-s6 1.03-1.15 ms (D140). A profile names where the time goes before any change.

## Acceptance criteria

- A1 A profile of the slowest cell names the top costs: a job
- A2 p99 below 1 ms in all four cells, exclusive interleaved runs against main (`docs/perf/first-read-gate.sh`): jobs
- A3 The suite and CI pass

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
