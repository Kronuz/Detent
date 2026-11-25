# paged-find-absent-cost: The paged table's absent find matches main's on names without a shared prefix

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status paged-find-absent-cost`.

## Outcome

first-read-1ms's review round 4 (R4-1) measured the paged `find` 1.08-1.16x main's warm `StoreTable::find` for absent names in tables whose names share no long prefix (random 4-16 byte names; +4.6 to +13.8 ns in the same binary; jobs 261125-033041-988a22 and 261125-034050-b0e901). D166 accepted up to 1.2x for the release. After this quest the cost is profiled and removed, so absent finds are within main's cost on every shape, or the measured floor of the two-level search is recorded with the profile.

## Acceptance criteria

- A1 A profile of absent finds on random 16-byte names names the extra cost: a job
- A2 A strict A/B with an A/A arm across D166's shapes: absent finds within main's floor, or the floor of the two-level search shown and recorded: jobs
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
