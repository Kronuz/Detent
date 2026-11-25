# msgpack-adoption: EX-3, the bounded MessagePack lexer in msgpack

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status msgpack-adoption`.

## Outcome

Detent's private lexical engine is deleted; `msgpack::bounded` at a public revision on msgpack's
main decodes, with 200-byte token storage and 512-byte encoding scratch, and the existing packer
encodes. Schema validation and resource scheduling stay in Detent. Captured payloads are
byte-identical and the snapshot encoding cost is resolved.

## Acceptance criteria

Sources: `docs/handoffs/bounded-codec.md` (the release-blocker brief, 2026-10-23); `track/msgpack-adoption:docs/status/msgpack-adoption.md`; `docs/EXTRACTION_CHECKLIST.md`
EX-3; `docs/RELEASE_PLAN.md` RC-1.

- A1 The private lexer and `detent_distribution/messagepack.hh` are gone; the bounded target configures without fetching its DOM dependencies: a build-and-grep job
- A2 [manual] `msgpack`'s bounded target (`msgpack:f8975e3` on `track/bounded-integration` today) is on msgpack's main with CI green: the lead names the run
- A3 The pin moves and `tools/check_pins.py` reports 0 mismatches: the `pins` gate
- A4 The full native suite, ASan/UBSan and TSan pass on immutable public sources: the `native`, `asan` and `tsan` gates
- A5 The fourteen captured logical payloads are byte-identical at 34-byte and 200-byte keys across raw and zstd framing (msgpack-adoption ledger): a job
- A6 The bounded decoder is fuzzed with zero findings (the decoder evidence the risk requires): a fuzz job
- A7 Snapshot encoding cost is resolved, or the remaining gap goes to the owner with numbers (EX-3; D7): `exclusive` jobs
- A8 [artifact] EX-3's row updated: `docs/EXTRACTION_CHECKLIST.md`
- A9 [manual] Library side (handoff, Acceptance): fragmented headers and payloads, zero and one budgets, length limits, signed extrema, token ownership, allocator preservation and allocation failure; bounded and DOM headers tested together; supported platforms and sanitizers: the lead names the msgpack commit and CI run
- A10 Caller error classification is unchanged, and decoded logical payloads are compared separately from compressed frames (handoff): a job
- A11 The default DOM APIs, exceptions and behavior are preserved for msgpack's other consumers (handoff): their tests as a job
- A12 Costs are measured on the actual production snapshot and delta paths with small fragments and maximum field lengths, before and after, with stated limits; a standalone packer benchmark does not count (handoff): `exclusive` jobs

## Contracts

Consumes `msgpack::bounded` and the packer. Wire bytes unchanged.

## Decisions

1. D5. Order EX-1, EX-2, EX-3 unless a measured dependency says otherwise (EXTRACTION_CHECKLIST).
2. `track/msgpack-adoption` (`Detent:a23f624`) is a source, not this quest's branch; scoped packer
   reuse was measured and discarded there.

## May land early

None.
