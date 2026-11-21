# managed-relay-ipv6-flake: test_system_managed_relay_ipv6 passes every time on macOS x86_64

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status managed-relay-ipv6-flake`.

## Outcome

test_system_managed_relay_ipv6 failed after 51 s on CI's macos-x86_64 on three unrelated quest branches (CI 44877100446 warm-relay-agent-boot, 44877822656 loop-idle-waits, 44869473548 journal-idle-groups) and passes on rerun. After this quest the cause is named (from the test's log and a reproduction) and fixed, in the test if the test is wrong, in the product if the relay is; if the fix is in product code outside this file set, the lead widens it.

## Acceptance criteria

- A1 The cause, with the log line or reproduction that shows it, is in the journal's Result: jobs or CI reruns named there
- A2 A job that reproduces the failure at the pre-fix commit (repeat runs, or the stress that exposes it) and the same job passing at the fix: a --fails-without job and a job
- A3 Ten macOS x86_64 CI passes of the test in a row at the fix (one run with the test repeated, or reruns): CI run ids
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
