# consensus-sanitizer-ci-time: Consensus sanitizer CI fits its timeouts: mutants and owe-order

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-sanitizer-ci-time`.

## Outcome

Two red consensus CI jobs not caused by the quests they ran for: (1) run 44989408833 attempt 2, linux-asan-ubsan ran its 60-minute limit and was cancelled with consensus_mutant_a-read-counts-every-acknowledgement still running (the per-test mutants from consensus:e35356a run in sanitizer builds, where they prove nothing a plain build doesn't); (2) run 44993461293, linux-tsan, consensus_transport_groups_owe-order aborted at 10.97 s after its gate step took 10.1 s for 2,000 groups. After this quest mutants run only where they mean something and fit, and owe-order's bound holds under TSan or names what it waits on.

## Acceptance criteria

- A1 Sanitizer builds run no consensus_mutant_* test (or each fits with the job's time left, measured), and plain builds still run all 17: a job listing ctest -N in both configurations
- A2 owe-order's cause is named; it passes 50 of 50 in a local TSan build under 4 burners, and the fix keeps what the test proves: jobs
- A3 The suite and CI pass
- A4 A mutant whose pattern no longer matches fails the mutant run loudly instead of passing silently, in test/driver_mutants.py and tools/mutants.sh alike; checkpoint-drops-suffix (stale since core.h reads `captured_ = std::make_unique<...>`, xapiand msg-243) applies again and is killed: a job that runs every mutant's apply step, plus the mutant's own run

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
