# Ask: reuse one bounded MessagePack lexer and existing encoder

Review a C++20 streaming service with a private allocation-bounded MessagePack codec. Move generic lexical decoding into the existing MessagePack library and reuse its existing packer for encoding. Recover any isolated migration and byte fixtures before starting over. This brief is independent of any application or repository.

## Scope

Provide a flat incremental lexer for array lengths, integers, strings, binary values, booleans and nil. One call consumes at most its byte allowance and emits at most one token. Zero allowance makes no progress. Enforce advertised lengths before allocating payloads. Tokens must not retain borrowed input views after the caller reuses its buffer.

The lexer owns no schema, nesting stack or complete-document buffer. Callers validate tuple arity, nesting, ordering, UTF-8 where required, profiles and aggregate counts. Explicitly state the difference between lexical completion and complete-document validity. Fixed token copying and dynamic growth need honest CPU/allocation bounds beyond input-byte accounting.

Encoding uses the library's existing packer through a small output adapter, without a second encoding engine. Failed output may leave partial scratch; caller completion gates must prevent publishing it. An adapter whose packer retains its address cannot be moved or copied unsafely.

## Acceptance and delivery

Test fragmented headers/payloads, zero/one budgets, length limits, signed extrema, token ownership, allocator preservation and allocation failure. Require exact valid production payload bytes and unchanged caller error classification. Compare decoded logical payloads separately from compressed frames; one does not prove the other identical.

Provide a lightweight bounded-only target that fetches no DOM dependencies. Preserve default DOM APIs, exceptions and behavior. Test bounded and DOM headers together, library consumers, supported platforms and sanitizers. Measure actual production snapshot/delta paths with small fragments and maximum field lengths; an unrelated standalone packer benchmark is insufficient.

Deliver shared tests, separate library and adoption commits, immutable revisions, removal of the private codec engine, reproducible before/after costs and stated limits. Preserve application schema/framing code. Follow repository rules and coordinate shared-host load. Do not keep an old private fallback or create another general-purpose codec.
