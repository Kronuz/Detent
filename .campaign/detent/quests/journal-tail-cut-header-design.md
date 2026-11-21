# journal-tail-cut-header-design: Design: can the turn log tell a damaged cut-header fragment from a tear in the newest tail

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-tail-cut-header-design`.

## Outcome

journal-tail-second-sector's durability review (F2, non-blocking; probe 261121-115044-fd4e5e) found that in the newest tail 36 of 210 crossing-fragment damages still drop the acknowledged turn without a report. All are the documented Limit: a fragment whose header the sector boundary cuts, with no whole header of its write before it, cannot be told from a tear under the sector model. F3 adds that the Limit's argument does not cover a host-chosen group id before a record's sequence and payload. This quest designs whether a format change closes it (for example a per-sector marker or a header that never straddles a sector), with the bytes and write cost per turn measured on the turn log's benchmarks, and recommends a change or accepting the Limit. Greenfield: no format compatibility is needed (D-greenfield).

## Acceptance criteria

- A1 [artifact] `docs/design/turn-log-cut-header.md`: the options, each with its space and write cost per turn measured at named commits, which of the 36 cases each closes, and a recommendation
- A2 A probe job showing each option's effect on the 210-case shape (prototype or model): jobs

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
