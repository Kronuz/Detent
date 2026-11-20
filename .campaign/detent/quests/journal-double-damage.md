# journal-double-damage: Recovery reports, never drops: a damaged block followed by a damaged header loses acknowledged records silently

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-double-damage`.

## Outcome

On journal main (1222e3b), when a damaged block is followed by a block whose header is also damaged, recovery reports no damage and drops acknowledged records (found by journal-idle-groups; probe at journal-wt/detent/journal-idle-groups/.scratch/probe/probe.cc). Recovery must report that damage and never silently lose an acknowledged record.

## Acceptance criteria

- A1 A test reproducing the probe fails at 1222e3b and passes with the fix (a job at each)
- A2 The crash model, extended with double damage at every block boundary, reports damage and loses no acknowledged record: a job
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
