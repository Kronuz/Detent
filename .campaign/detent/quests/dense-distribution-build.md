# dense-distribution-build: Dense distribution, built: agents and relays on v2, the full copy and large deltas ship as verified pages

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-distribution-build`.

## Outcome

One feature (owner, msg-145; D133, D134, D135): docs/design/dense-distribution.md built end to end, as written. It folds the former step quests dd1-transfer-receiver, dd2-canonical-copy, dd3-full-copy-pages, dd4-delta-transfers, namespace-local-cursor and namespace-s6-v1-removal; their branches are this quest's tasks and merge into its branch.

- The local layer runs on the one W1 cursor; store layout 2 and `DTROOT04`; older stores and flat-v1 are refused and the agent bootstraps fresh.
- Agents and relays consume v2 (W5/W6) sessions only; the v1 request, one-shot receive and poll mode are gone.
- A full copy ships as a transfer (the pages the receiver lacks, in seal order, derived fields zero); the agent verifies it against one root digest and writes it, building nothing; a root mismatch falls back to records and a fetch resumes from its offset.
- Each update goes in the smaller encoding (D6): records for scattered changes, pages for large clustered ones; stale receivers get records above the cut and the full copy below (D36).

Build the design as written; deviate only where a measurement shows it is not optimal or efficient, and report that first. Review (security for the transfer decoder, correctness, durability) and the cold-boot measurement against records run once on the finished feature, after it lands.

## Acceptance criteria

- A1 The suite passes at the quest's head (the gate check), including tests that the receiver reproduces the model's 100K full-copy transfer byte for byte, refuses each case of the design's D4 table, and that a 100K cold agent runs no packer.
- A2 CI is green at the head.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
