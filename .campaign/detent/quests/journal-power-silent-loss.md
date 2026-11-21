# journal-power-silent-loss: T9 power loss on faultfs never loses an acknowledged record silently

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-power-silent-loss`.

## Outcome

Track C's T9 rerun at journal main 178030b (msg-183) failed 7 checks on macOS (job 261120-151854-18b807, 200 seeds) and more on FreeBSD (261120-152238-d74166): "the damage is reported" and "a group not lost keeps every acknowledged record" at block_bytes 2048 'last' seeds 145 and 199, 256 'both' seed 122, 512 'both' seed 140. The same row passed at d90797c (261118-115852-e136e7). After this quest the cause is named (regression from journal-double-damage, or the F2/F3 residual its review left as non-blocking, now newly checked), and no damage pattern in T9 loses an acknowledged record without reporting it.

## Acceptance criteria

- A1 [artifact] The cause per failing seed, with the damage pattern and whether d90797c or 178030b checks it: the journal's Result
- A2 Every failing seed passes at the fix, and the 200-seed T9 row passes on macOS and in the FreeBSD VM: jobs
- A3 A mutant restoring the cause fails a named seed: a --fails-without job
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
