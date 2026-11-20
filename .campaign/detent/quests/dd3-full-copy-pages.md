# dd3-full-copy-pages: Dense distribution DD3: the full copy ships as pages

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dd3-full-copy-pages`.

## Outcome

D133, D34. Build dense-distribution.md's DD3: namespace W6's frame header gains the encoding and the 32-byte target commitment; the relay's copy service sends a full copy as a transfer; the agent's full-copy path receives it through DD1's receiver and builds nothing. A root mismatch falls back to records.


**Build the design as written** (owner, 2026-11-20): docs/design/dense-distribution.md is the spec. Deviate only where a measurement shows the design is not optimal or efficient; write that measurement in the journal under "Deviation" and report it before changing course.

## Acceptance criteria

- A1 At 5M cold bootstrap the agent runs no packer (a counter at zero) and receives 25,705,725 B within 1%; its packs equal the relay's copy, with derived ids at ordinal 0.
- A2 Bytes per namespace are measured for each of the corpus's four namespaces against their record copies.
- A3 A root mismatch falls back to records, and a resumed fetch continues from its offset: system tests that fail without each.
- A5 The page transfer greatly beats the records arm on the agent's cold full copy (CPU and wall, same fixture, exclusive), stated with its magnitude; if it does not, the quest stops and reports with the numbers instead of landing.
- A4 The agent's CPU and wall against the records arm under `quiet exclusive` (D85: 100K rounds for spread, one 5M confirmation round), and Q3's two framings for both encodings, in docs/perf/full-copy-pages.md.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
