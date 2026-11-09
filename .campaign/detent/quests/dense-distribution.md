# dense-distribution: Dense distribution design: ship verified pages, not records

Risk: low (the quest lands a design document; the protocol it designs is built by later high-risk quests). The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-distribution`.

## Outcome

`docs/design/dense-distribution.md` decides how DTPAGE03 state reaches relays and agents so that a
receiver builds nothing: a full copy ships the canonical packs, a delta ships the pages the new
state has and the base lacks (or records, when they are smaller), and every receiver checks the one
root digest. It states the wire shape, the per-update choice between pages and records, what a
relay stores and serves, how an agent fetches only the pages it lacks, and the first buildable
steps with their criteria, each number measured on the real 5M corpus with the dense model.

## Acceptance criteria

- A1 [artifact] The full-copy cost at 5M: pack bytes raw and zstd against the records' wire bytes, and the receiver's work (SHA-256 bytes, writes) against today's build, measured with `docs/design/dense-pages-model.py` on the r12 corpus: `docs/design/dense-distribution.md`
- A2 [artifact] The delta cost at 5M for one update, 10K uniform, 10K clustered and the T4 mixed churn: new-page bytes against record bytes, and the choice rule (pages or records per update) with its crossover: `docs/design/dense-distribution.md`
- A3 [artifact] The wire and storage shape: what a Snapshot and a Delta carry, how pages are named (digest, derived file ids from namespace-delivery D8), what a relay stores and serves without building, how a receiver verifies against the root digest and refuses a bad page: `docs/design/dense-distribution.md`
- A4 [artifact] Failure and resume: a partial fetch, a missing or corrupt page, a relay that lacks a page, and the fallback to records: `docs/design/dense-distribution.md`
- A5 [artifact] Build plan: the first buildable steps as quests (files, criteria, risk) and what each needs from dense-pages, reader S6 and namespace S4-S10: `docs/design/dense-distribution.md`
- A6 The model runs that produce A1 and A2 are campaign jobs (`--commit` at the branch head) whose outputs the document cites by job id

## Contracts

Consumes DTPAGE03 and DTROOT03 as `docs/design/dense-pages.md` defines them, the wire framing of
`docs/HOW_IT_WORKS.md` (MessagePack records in fragments of at most 64 KiB), and namespace-delivery
D8 (derived file ids) and D10 (first touch, scrub, repair). Provides the design later quests build.

## Decisions

1. The owner withdrew the "agents should not have to build anything" direction (2026-11-09, D33).
   The design stands on its measurements (receiver work and bytes per encoding) as a record for
   later quests; no implementation quest starts from it until a goal asks for one.
2. Projected starting points, to be replaced by measurements: full copy 31.7 MB of zstd-1 packs
   against 25.6 MB of records as sent; 10K uniform delta 42.8 MB of pages against about 0.5 MB of
   records; 10K clustered 0.6 MB.
3. One state, one root digest, two encodings: the receiver's verification is the same whichever
   encoding arrives (the owner's one-shape rule).
4. Keys stay generic (PLAN.md, Candidate uses): page and key naming on the wire must not assume text keys or NUL-free names, and must leave room for ordered (floor, prefix) lookups.
5. The design is reviewed once (lens `design`) before any implementation quest starts.

## May land early

None.
