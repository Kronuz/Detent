# managed-failover-isolated-leader: Agents opened at an isolated former leader find the majority (failover gate 2)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status managed-failover-isolated-leader`.

## Outcome

docs/MANAGED_FAILOVER_ARCHITECTURE.md acceptance gate 2: "A separate partition leaves the former leader's client HTTPS reachable while cutting peer links. Idle responses cannot prevent discovery of the functioning majority." It does not hold on main. old-flakes-disposition's probe 261121-203249-b164cf opened the agents at the leader the partition then isolates: 8 of 8 runs never converge in 180 s (every namespace stays at 1 of 5), against a bound of about 1 s refresh plus 2 s per member deadline plus 100 ms backoff. At timeout voter 0 (the isolated old leader) is role 0, not fenced, and has sent 650 idle responses; no agent logged a refresh failure or a source rotation. This is CI flake 3 (test_system_managed_partition, about 1 in 50 when leadership lands on the agents' voter).

After this quest an agent whose source is a voter cut off from the majority rotates to the majority within the documented bound, whether that voter answers idle, answers not-leader, or does not answer.

## Acceptance criteria

- A1 The cause is named in the journal with the code path (why idle responses from the isolated voter keep the agent there), and a deterministic test reproduces it: it fails at main and passes at the fix (`--fails-without`)
- A2 The forced partition probe (agents opened at the isolated leader, old-flakes-disposition's managed.py diagnostic) converges in 8 of 8 runs, with each agent's convergence time within the documented bound: a job
- A3 test_system_managed_partition and failover pass x50 at the fix, plain and under TSan: jobs
- A4 [artifact] docs/MANAGED_FAILOVER_ARCHITECTURE.md states the discovery bound the tests assert
- A5 The suite and CI pass

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
