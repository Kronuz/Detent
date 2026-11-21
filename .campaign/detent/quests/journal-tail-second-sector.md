# journal-tail-second-sector: Damage in a newest-tail fragment's second sector is reported, never a silent tear

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-tail-second-sector`.

## Outcome

The journal-power-silent-loss durability review (F1, probes 261120-212732-0aad27 and 261120-213315-944786) found that a fragment spanning two sectors, damaged in its second sector, in the newest tail, drops an acknowledged turn unreported in 175 of 334 cases (the same at 178030b). The crash model (R7) treats it as a tear, but a fragment that verifies in that second sector proves the sector was written after the damaged one's predecessor, so it is damage. After this quest such damage is reported, and a real tear is still dropped silently.

## Acceptance criteria

- A1 The review's probe shapes report damage in every case where a later verifying fragment proves the sector was written; no acknowledged turn is dropped unreported: jobs
- A2 Real tears (the T9 crash points and simulated tears) still report no damage: the 200-seed T9 row on macOS and in the FreeBSD VM: jobs
- A3 A mutant restoring the tear reading fails A1's probe: a --fails-without job
- A4 [artifact] The crash model's rule for the newest tail, updated: docs/mechanisms
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
