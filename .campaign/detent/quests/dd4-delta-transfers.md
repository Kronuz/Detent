# dd4-delta-transfers: Dense distribution DD4: delta transfers and the pages-or-records rule

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dd4-delta-transfers`.

## Outcome

D133, D6. Build dense-distribution.md's DD4: the relay's page window and Merkle diff, D6's rule choosing pages or records per update, and the delta receiver.

## Acceptance criteria

- A1 At 5M the transfers for the four scenarios of job 261109-030635-4e209f have exactly the model's pages (5; 16,489; 369; 16,616) and bytes.
- A2 Against S8's block answers, the rule picks records for every `u:` row and pages for `c:1000000`, and rule 1's shortcut holds on the measured record sizes at the crossover.
- A3 A base page that fails first touch during a delta leads to namespace D10.3's full copy as pages, and reads recover.
- A4 A refused transfer falls back to records without a wrong read: a test that fails without the fallback.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
