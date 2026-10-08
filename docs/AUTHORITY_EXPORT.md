# Authorized authority export

**Status:** Scope authorization, frozen projection and incremental snapshot encoding are implemented as libraries. The `/v2/distribution` service endpoint, applied-change history, transport streaming and production agent integration remain pending. The [delivery blueprint](DELIVERY_INTEGRATION_ARCHITECTURE.md) governs the assembled component and its final review.

## Scope identity

`AuthorizedScope::authorize()` validates requested names against a trusted allowlist, sorts them and removes duplicates. An empty request produces an empty scope. Admission bounds the original requested count to 4,096 names and each name to 198 bytes.

The shared [`canonical_scope_identity()`](../include/detent/scope.hh) hashes the fixed domain `detent-authorized-scope-v1`, followed by the stable configured principal, nonzero policy epoch and canonical names. Integers and string lengths use eight-byte big-endian encoding; the name count precedes the individual length-framed names. SHA-256 supplies the complete 32-byte identity. Credential tokens do not enter this calculation. Changing grants requires a policy-epoch change; rotating a token can preserve the principal and scope identity.

Authorized names and their controls use an actual `StorageLedger`. Allocator headers, page capacity and shared controls remain charged through their physical lifetime, including surviving weak owners. A Python `hashlib` calculation supplies an independent fixed vector for the C++ test.

## Frozen public projection

`State::capture()` freezes the authority context and global revision with the authorized scope, sharing only record and namespace roots. Producer sessions and receipt state are excluded. Lower-bound cursors traverse one namespace at a time, preserving canonical namespace order even when global key ordering differs (for example, `app2:key` sorts before `app:key`). Cursors retain their namespace storage, immutable roots and originating retirement arenas.

The projection retains whole record/namespace roots, including unauthorized records that remain inaccessible through its cursor interface. These authority allocations retain the existing logical/cardinality limits; they are not represented as estimated physical ledger charges. Production integration must admit one distribution capture slot and keep it occupied through retirement.

After emission, cancellation or replacement snapshot activation, the owner drops encoder/cursor roots, calls `retire()` and advances `collect()` with its shared work budget. The projection retains the originating definition arena until pending node/value reclamation drains. The snapshot-replacement fixture releases a boolean with 256 overrides through at least 256 collection turns. Capture and cursor ownership remain on the authority executor; transport receives independently owned bytes.

## Snapshot encoding

`SnapshotExportEncoder` counts namespace, record and override rows against the same frozen projection before emission. It checks logical schema limits and calculates the exact encoded byte length for subsequent frame metadata. Counting and emission use caller work budgets, including one-byte budgets. An independent tuple-stream oracle checks output byte equivalence.

The encoder emits the existing version-1 MessagePack snapshot schema and profile through one pending output slot. Each immutable output chunk contains at most 65,536 initialized bytes. A final partial chunk retains the full backing-capacity charge; unused capacity is never exposed as payload. A held chunk remains charged across subsequent allocation refusal and encoder disposal. The coordinator retires the original capture separately.

The reusable MessagePack writers accept fixed inline scratch while preserving their existing concrete `std::string` overloads. `OwnedBuffer::seal_prefix()` adds immutable partial-chunk publication without changing the existing complete-buffer `seal()` contract. Authority projection remains independent of the codec; the separate `detent::authority_export` target adds snapshot encoding.

## Qualification and remaining work

The eight focused authority/codec suites pass on macOS (1.41 seconds), Linux (1.11 seconds) and FreeBSD (1.45 seconds), under ASan/UBSan (6.68 seconds) and fully instrumented TSan (9.78 seconds). Coverage includes frozen export during writes, authorization isolation, canonical hashing, randomized lower-bound traversal, replacement-snapshot retirement, independent MessagePack vectors, multi-chunk output and retained-allocation refusal. Final empty-scope and schema-limit rejection checks also pass separately on all three platforms and under both sanitizers.

These tests establish library behavior. Applied-change coverage, owner-slot integration, authentic transport settlement and end-to-end authority/agent/SDK qualification remain required before Stage 4 acceptance. No delivery throughput or fleet-capacity claim follows from these fixtures.
