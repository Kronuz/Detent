# restart-capacity: Raise authority capacity at a coordinated restart

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status restart-capacity`.

## Outcome

Operators raise authority limits (keys and the other nine) by restarting the voters with a raised configuration; nothing on disk or the wire changes, relays and agents keep their generations and stores (D141). Each `DTABAT02` and `DTAADM01` entry replays under the limits it carries; only the equality checks change. Contract: `docs/ONLINE_CAPACITY_ARCHITECTURE.md` rules 1-7.

## Acceptance criteria

- A1 A group that failed `Capacity` at the old key ceiling replays after the raise with the same result: jobs, `--fails-without` a mutant applying under configured limits
- A2 An image and an administrative entry recorded under the old profile recover under the raised configuration; a lowered field refuses and names it: jobs
- A3 A retry of a pre-raise producer sequence returns its original receipt: jobs
- A4 Three voters restarted with raised `keys` grow past the old ceiling; relays and agents keep their generations and serve the new keys: a system job
- A5 [artifact] Every `State::limits_` site classified replicated or local: `docs/mechanisms/authority-capacity-raise.md`
- A6 The suite, sanitizers and CI pass

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
