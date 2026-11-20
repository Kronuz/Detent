# warm-relay-agent-boot: agent cold bootstrap against a warm relay at 100K, 1M and 5M

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status warm-relay-agent-boot`.

## Outcome

The time a new agent pays to join a fleet: one agent cold-bootstrapping against a relay that already holds the bundle (started, durable bundles ready, then the clock starts), at 100K, 1M and 5M records on current main. Today's numbers (dense-sync-stack 136-163 s at 5M; cold-bootstrap-5m.md 754 s) start the relay and the agent under one clock, so they include the relay's own catch-up from the authority; cold-bootstrap-5m.md's phase split put the agent at 32% (100K, main) and 38% (5M, r12 binaries) of that total, an estimate for today's code. This quest measures it directly, with the agent phase split (fetch, decode and build, publish and sync, select) where counters allow.

## Acceptance criteria

- A1 Exclusive jobs on current main: per size, the relay starts and reaches "durable bundles ready" outside the clock, then at least 3 interleaved rounds time one cold agent from start to selecting the fixture's revision; no "ran beside" record. The harness lives under docs/measurements/warm-relay-agent-boot/.
- A2 [artifact] docs/perf/warm-relay-agent-boot.md: per size, the median and range of the agent's warm-relay bootstrap, its CPU and wall, files, device flushes and wire bytes, the relay-plus-agent total from the same fixture for comparison, the hardware, and the job ids.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
