# relay-service-layouts-base-flake: test_relay_service: the layouts sometimes never reach a base (:790)

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status relay-service-layouts-base-flake`.

## Outcome

test_relay_service.py:790 (`kinds == {"base", "block", "section"}`) fails intermittently: pin-wave's native job 261125-035857-8ad10f, and 7 earlier jobs on commits without pin-wave's pins (legacy-h, managed-failover and system-failover shards; 261123-175720-145715 among them, cited by relay-service-refused-sync-flake for :782 though it failed at :790). Per pin-wave's journal ruling 28, the authority behaves as designed (D136): its 1 s `seal_after` timer seals at write pace, writes take about 0.5 s, and a base needs a cut, so whether a base appears depends on timing. After this quest the case makes a base deterministically (it drives the cut the base needs), and the check is unchanged (D152).

## Acceptance criteria

- A1 The case passes 50 of 50 strict runs at the fix, and a run that forces the timing (a `--fails-without` job) fails at main.
- A2 The suite passes and `ci:ci` is green at the landed commit.

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
