# system-failover-port-reuse: test_system_managed_authority_failover: the restarted leader's bind fails with Address already in use

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status system-failover-port-reuse`.

## Outcome

CI run 44989878521 (restart-capacity's branch, ASan) failed test_system_managed_authority_failover because the killed leader's restart died with 'bind: Address already in use' (restart-capacity's reviewer, Detent:e433d313). This is not D144's isolated-voter defect. After this quest a restarted process in the system tests either rebinds its port (SO_REUSEADDR in the server, if it lacks it) or the harness waits for the port, whichever is the real cause.

## Acceptance criteria

- A1 The cause is named from the server's socket options and the harness; a test or loop that hit it fails before and passes after: jobs\n- A2 The suite and CI pass

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
