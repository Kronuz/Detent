# consensus-macos-x86-loop-kernel-wait: groups fetch-alongside: a loop stuck in a kernel wait for 1.5 s on macOS x86 CI

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-macos-x86-loop-kernel-wait`.

## Outcome

On GitHub's macos-x86_64 runner, groups_test's fetch-alongside-consensus case aborts when the watchdog sees a loop that has not turned for about 1.5 s during the 8 MiB fetch rounds: main at consensus:c724c58 (CI 45542770252, node 2 loop 1, 1511 ms) and consensus-inline-sync-loop at consensus:9f14194 (CI 45594592100, node 3 loop 3, 1515 ms). The stuck loop is in state U, a kernel wait; other idle loops turn within 1 ms, and links end with the term lost. It has not reproduced locally (0 of 100 strict runs). consensus:b2fa03b makes the probe take a kernel spindump when the stuck loop is in state U. After this quest the cause is named from such a dump and fixed, or shown to be the runner's, with the case passing on macos-x86_64.

## Acceptance criteria

- A1 [manual] The cause, named from a spindump of the stuck loop on macos-x86_64 CI: the frame it waits in and why.
- A2 A test that fails without the fix (a `--fails-without` job), or, if the cause is the runner, [manual] the evidence that it is.
- A3 The suite passes and `ci:ci` is green at the landed commit, macos-x86_64 included.

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
