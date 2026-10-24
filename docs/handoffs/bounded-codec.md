# Ask: reuse one bounded MessagePack lexer and existing encoder

**Priority:** P2 · Rank 6 of 7 · Required codec adoption.

Required before release. The bounded codec has an isolated implementation and byte fixtures; prioritize adoption over a new parser design.

## Repositories and starting point

1. **Start repository:** [Kronuz/msgpack](https://github.com/Kronuz/msgpack).
2. **Consumer integration:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
3. **Existing blueprint:** [Kronuz/Detent/blob/main/docs/MSGPACK_EXTRACTION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/MSGPACK_EXTRACTION_ARCHITECTURE.md).
4. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** Shared bounded lexer/target and existing packer; consumer `include/detent_distribution/messagepack.hh` and production encoders/decoders under `src/detent_distribution/`. Existing byte and compatibility evidence: `benchmarks/msgpack-extraction-preliminary-20261019.json`; capture harness: `benchmarks/capture_production_wire.py`.

**Existing local candidate:** `.scratch/library-adoption-20261017/detent-msgpack-extraction` relative to the consumer checkout, if available. Inspect both tracked and uncommitted work. A fresh clone will not contain this scratch worktree; use the linked committed blueprint and evidence to recover the migration, and do not assume it is already on main.

**Dependencies and ownership:** Audit may start now. Preserve resource ownership APIs established by the resource task; keep final adoption separate from HTTP changes so regressions remain attributable.

**Release-unblocking outcome:** One shared lexer and existing packer adopted, private codec engine removed, exact production logical payload bytes preserved, bounded-only dependency closure and composed platform/cost checks passing.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 streaming service with a private allocation-bounded MessagePack codec. Move generic lexical decoding into the existing MessagePack library and reuse its existing packer for encoding. Recover any isolated migration and byte fixtures before starting over.

## Scope

Provide a flat incremental lexer for array lengths, integers, strings, binary values, booleans and nil. One call consumes at most its byte allowance and emits at most one token. Zero allowance makes no progress. Enforce advertised lengths before allocating payloads. Tokens must not retain borrowed input views after the caller reuses its buffer.

The lexer owns no schema, nesting stack or complete-document buffer. Callers validate tuple arity, nesting, ordering, UTF-8 where required, profiles and aggregate counts. Explicitly state the difference between lexical completion and complete-document validity. Fixed token copying and dynamic growth need honest CPU/allocation bounds beyond input-byte accounting.

Encoding uses the library's existing packer through a small output adapter, without a second encoding engine. Failed output may leave partial scratch; caller completion gates must prevent publishing it. An adapter whose packer retains its address cannot be moved or copied unsafely.

## Acceptance and delivery

Test fragmented headers/payloads, zero/one budgets, length limits, signed extrema, token ownership, allocator preservation and allocation failure. Require exact valid production payload bytes and unchanged caller error classification. Compare decoded logical payloads separately from compressed frames; one does not prove the other identical.

Provide a lightweight bounded-only target that fetches no DOM dependencies. Preserve default DOM APIs, exceptions and behavior. Test bounded and DOM headers together, library consumers, supported platforms and sanitizers. Measure actual production snapshot/delta paths with small fragments and maximum field lengths; an unrelated standalone packer benchmark is insufficient.

Deliver shared tests, separate library and adoption commits, immutable revisions, removal of the private codec engine, reproducible before/after costs and stated limits. Preserve application schema/framing code. Follow repository rules and coordinate shared-host load. Do not keep an old private fallback or create another general-purpose codec.
