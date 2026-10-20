# Bounded MessagePack extraction

**Updated:** 2026-10-19. **Status:** Validated Architectural Blueprint, accepted for implementation. Isolated source preparation is in progress; builds, tests and measurements remain open.

Move the flat incremental lexer and fixed scratch into `Kronuz/msgpack`. Encoding uses its existing packer through an append-to-write adapter. Detent keeps schema validation, storage admission and scheduling. Remove the private codec and its encoding engine after migrating callers.

## Boundary and scale

Expose a header-only `msgpack::bounded` target before DOM dependency population. `MSGPACK_BUILD_DOM` defaults to ON, preserving the COW value, patcher, exception seam and existing public targets. With DOM disabled, configuration must fetch none of RapidJSON or the support libraries used by that DOM. Keep bundled `pack.hpp` unchanged; checked string-view convenience on the output adapter delegates to its existing header/body methods.

The lexer emits flat array lengths, integers, strings, binary values, booleans and nil without a nesting stack, recursion, retained input views or a complete-document buffer. Enforce advertised lengths before growing payload storage. One call consumes at most its supplied byte allowance and emits at most one token. A zero allowance makes no progress.

Byte accounting bounds input consumption. Fixed token emission can copy its populated prefix, and dynamic growth can allocate or copy additional bytes. Detent retains its 200-byte token storage and 512-byte encoding scratch; lexical byte accounting must not be presented as a bound on arbitrary allocators or schema processing.

## Ownership and failure

Tokens own their payload. Detent copies schema views into admitted arena storage before token destruction. Allocator-bearing storage can be supplied explicitly; emission swaps with an empty token constructed using that same allocator, and reset clears the retained storage without substituting a default allocator. Fixed scratch has no allocator dependency.

Malformed input and storage failure fence the decoder. Preserve unsigned limits, signed extrema and subtraction checks. `finish()` checks partial lexical tokens only: empty input and a bare array header can pass. Detent continues validating complete structure, tuple arity, scope, ordering, UTF-8, profiles and aggregate counts. Existing full MessagePack parsing and its exception hierarchy stay intact; the bounded lexer uses standard exceptions.

An output failure may leave partial caller-owned scratch. Existing producer completion gates must prevent publication of that partial encoding. The adapter borrows its synchronous output and cannot be copied or moved because the underlying packer retains its stream address.

## Migration and compatibility

Detent's replacement header contains only the local wire-type aliases. Production encoders and test fixtures call the canonical packer API; no old private fallback remains. The removed `detent_distribution/messagepack.hh` codec is replaced by `msgpack/bounded.hpp` and the `msgpack::bounded` target. This is a source migration for consumers of that internal codec header. Preserve valid wire bytes and existing Detent error classification.

Xapiand continues using the full DOM and streaming unpacker with its current exception seam. Qualify bounded and DOM headers in one translation unit. No bounded runtime library, shared ABI or allocator engine is added. Publish the library and catalog together, then adopt its immutable revision; local overrides alone cannot close adoption.

## Completion gates

1. Move lexical tests and independent byte vectors into the library. Cover header and payload fragmentation, zero/one budgets, capacity and input limits, signed extrema, owning token lifetime, allocator preservation and injected allocation failures. Explicitly test lexical-only finish behavior.
2. Keep Detent's schema, storage, framing, snapshot/delta encoding and disposal tests. Require byte-identical production output hashes and semantic results before and after migration.
3. Qualify native macOS/Linux/FreeBSD and ASan/UBSan, a fresh bounded-only configuration with no DOM fetches, default DOM configuration, header coexistence and clean Xapiand compatibility.
4. Measure actual production snapshot/delta paths using realistic and maximum field lengths with small fragments. Record allocations, peak admitted bytes, CPU and maximum owner-turn latency in alternating pairs. The codec trial's separate `Pack` implementation does not exercise the production lexer and cannot establish extraction cost.
5. Review the complete component after the source and qualification gates. HTTP and sustained fleet qualification remain independent release requirements.
