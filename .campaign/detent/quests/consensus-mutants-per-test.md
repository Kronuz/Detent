# consensus-mutants-per-test: Each consensus mutant is its own ctest with its own timeout

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-mutants-per-test`.

## Outcome

`tools/mutants.sh BUILD SEEDS STEPS [NAME]` runs one mutant; CMake adds one `consensus_mutant_<name>` test per mutant, each with a TIMEOUT sized from its measured time, so ctest -j runs them in parallel and a slow one names itself (msg-170: the single 1800 s test ran out of time with 9-14 of 16 verdicts on loaded hosts).

## Acceptance criteria

- A1 Every mutant is its own ctest and each is killed: a job on macOS and one in the Linux VM
- A2 Each TIMEOUT is at least twice its mutant's measured time on the gate VM (track-c-gates' jobs 261120-152025-9d8f43 and 261120-152033-26cdba give the times): [artifact] the CMake list
- A3 CI passes on every job, FreeBSD included: `tools/mutants.sh` runs without bash there (POSIX sh, or the test is added only where bash exists)

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

- Lead, 2026-11-21: urgent. cpp-libraries:12bae98 (shared CI configures with Ninja) turned on `consensus_mutants` in CI, so consensus main is red at 8780e44: it times out at 1,800 s on four jobs and fails on FreeBSD with `env: bash: No such file or directory` (run 44951962478). This quest is the fix forward.

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>

- 2026-11-21, msg-190 (xapiand track-c-gates, consensus 8801b99, tools/mutants.sh BUILD 400 10000, 16/16 killed each): per-mutant seconds in mutants.sh order. macOS 261120-151941-ce0b7a (2,616 s): 91 115 173 107 163 114 91 105 160 81 428 106 190 254 211 224 (longest "repair ignores the term at the base boundary" 428). Linux VM 261120-200605-743b1d (1,900 s): 127 122 175 132 116 117 110 117 114 100 110 107 100 100 111 140. FreeBSD VM 261120-200607-b7499c (1,764 s): 113 95 94 102 98 94 98 109 140 135 123 122 100 104 114 121. Size per-mutant TIMEOUTs from these (about 2x the slowest platform per mutant).
