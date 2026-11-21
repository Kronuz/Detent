# consensus-leaving-removal: A leader proposes a leaving member's removal only from an event that reserved its append

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-leaving-removal`.

## Outcome

Xapiand's placement repair (msg-189) found a leader fencing with "persistence lacks preowned reservation" when Core::drive proposes D3's removal of a committed.leaving member: Worker::submit_retained admits a leader's Tick with no append reservation (`if (leader && Tick) plan.count = 0`), yet drive on that Tick, or on the AppendResponse that commits the promotion, calls propose_membership and emits a Persist, so ingest throws. Seen at consensus b8de3b8 and main bb961b3; repro job 261121-003512-50dacf (strata placement--repair, replace seed 1); probe at consensus fd8d977 (job 261121-004526-522f59). After this quest the core proposes the removal only from an admitted event that reserved one configuration append (the next Tick planned with it when a removal is due, option (b)), and a replacement through the Driver completes.

## Acceptance criteria

- A1 A Driver-level replacement test (add the new member, promote, remove the leaving one) completes over many seeds with no fence, and the removal is committed: jobs
- A2 A mutant that proposes the removal from an unreserved event fails A1 with the fence: a --fails-without job
- A3 Xapiand's repro (strata placement--repair replace seed 1, job 261121-003512-50dacf's command) passes at this commit pinned in: a job
- A4 CI passes

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
