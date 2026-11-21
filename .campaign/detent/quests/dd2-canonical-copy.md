# dd2-canonical-copy: Dense distribution DD2: seal-order relocation into the canonical copy

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dd2-canonical-copy`.

## Outcome

D133. Build dense-distribution.md's DD2: `src/detent_local/paged_canonical_copy.{hh,cc}` relocates a generation's pages into seal order, the full build's layout, without computing a page digest.


**Build the design as written** (owner, 2026-11-20): docs/design/dense-distribution.md is the spec. Deviate only where a measurement shows the design is not optimal or efficient; write that measurement in the journal under "Deviation" and report it before changing course.

## Acceptance criteria

- A1 After dense-pages' T4 churn applied through dense-pages D7, relocating into seal order gives the full build's pages in the full build's order and offsets, every byte outside placement equal.
- A2 Zero page digests are computed during relocation (dense-pages T7's counter), checked by a test.
- A3 A test fails without the relocation's ordering rule (a `--fails-without` job).

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
