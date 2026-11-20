# consensus-mutants-per-test: Each consensus mutant is its own ctest with its own timeout

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-mutants-per-test`.

## Outcome

`tools/mutants.sh BUILD SEEDS STEPS [NAME]` runs one mutant; CMake adds one `consensus_mutant_<name>` test per mutant, each with a TIMEOUT sized from its measured time, so ctest -j runs them in parallel and a slow one names itself (msg-170: the single 1800 s test ran out of time with 9-14 of 16 verdicts on loaded hosts).

## Acceptance criteria

- A1 Every mutant is its own ctest and each is killed: a job on macOS and one in the Linux VM
- A2 Each TIMEOUT is at least twice its mutant's measured time on the gate VM (track-c-gates' jobs 261120-152025-9d8f43 and 261120-152033-26cdba give the times): [artifact] the CMake list
- A3 CI passes

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
