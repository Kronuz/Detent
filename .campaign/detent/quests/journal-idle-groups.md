# journal-idle-groups: Idle groups leave the log: parked groups cost no stream, re-partitioned streams, group creation joins the turn (D148 Q3)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-idle-groups`.

## Outcome

Xapiand needs a host to hold millions of open indexes (D148 Q3, Xapiand PLAN.md; strata docs/design/rocksdb-parity.md at strata:0d93765c). After turn-log-parity puts one log and one flush on each device, an idle group must leave that log: it is parked with no stream state, and it rejoins on its next write. Streams re-partition as groups park and wake, and creating a group joins the current turn rather than paying its own flush.

The design comes first (docs/design/idle-groups.md in journal), built on consensus-quiescence's closed-state hard-state bytes (its A5). One review then covers the finished feature before it lands (D135).

## Acceptance criteria

- A1 [artifact] `docs/design/idle-groups.md`: what a parked group keeps on disk and in memory (bytes per group), how it wakes and rejoins a stream, how streams re-partition with no lost or reordered entry, how group creation joins a turn, and crash recovery at each step
- A2 A parked group holds no stream and no per-group heap beyond the bytes A1 names: a counting test at 1M parked groups
- A3 Wake, re-partition and create keep every acknowledged entry across a crash at each step: the crash-model job
- A4 The suite and CI pass

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
