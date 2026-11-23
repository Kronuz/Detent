# local-disk-store-lease-flake: test_local_disk_store line 191 failed once on CI macos-arm64

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status local-disk-store-lease-flake`.

## Outcome

test_local_disk_store failed once on CI macos-arm64 (run 44989878521, job 137116872503, restart-capacity's branch at Detent:38d36498, which touches no local store code): "disk store invariant at line 191", the check that a generation whose shared lease was closed is gone after request_collection() and drive(). Main's recent CI is green. Find whether collection can leave the generation for one drive pass (a test that asserts too early, or a store that misses the lease release), and fix the cause.

## Acceptance criteria

- A1 The cause is named, and a test reproduces it, or a loop of 2,000 runs under 4 burners passes at the fix while failing before it: jobs
- A2 The suite and CI pass

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
