# consensus-quiesce-election-stall: With quiescence on, a group whose removal committed elects a leader within its election bound

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-quiesce-election-stall`.

## Outcome

consensus-leaving-removal's correctness review (F5, non-blocking) found that with Timing::quiesce on, replace seeds 1-1079 finish, then seed 1080 elects no leader for 3000 settle steps after the leaving member's removal committed (probe 261121-084426-67006a, trace 261121-084651-05c124; rerun `driver_replace_test 1 1080` with quiesce on). consensus-quiescence (landed, consensus:5ebf834) owns rest and wake; this is a liveness defect in it, not attributed further. After this quest the cause is named and fixed, and a group that loses its leader while followers rest elects one within the election bound in every seed.

## Acceptance criteria

- A1 driver_replace_test with quiesce on, seeds 1-3000: every seed elects a leader within the election bound after the removal; the seed-1080 case as a named test: jobs
- A2 A --fails-without job at the pre-fix commit shows seed 1080 stalling
- A3 The quiescence suite and TSan pass: jobs
- A4 [artifact] docs/mechanisms names the wake rule that covers the case
- A5 CI passes

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
