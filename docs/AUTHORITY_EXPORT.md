# Authorized authority export

**Status:** Scope authorization, frozen projection, incremental snapshot/delta/progress encoding and bounded applied-change history are implemented as libraries. The `/v2/distribution` service endpoint, transport streaming and production agent integration remain pending. The [delivery blueprint](DELIVERY_INTEGRATION_ARCHITECTURE.md) governs the assembled component and its final review.

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

## Applied-change history

`AppliedChangeRecorder` is an optional authority hook. Changed transactions materialize their cache payload against the unpublished candidate with resumable work budgets. A batch exposes these payloads only after Node installs its authoritative state. Duplicate producer receipts and unchanged mutations create no additional history. Recovery, snapshot activation and abandoned batches invalidate or retire optional history without changing authoritative results.

`ChangeHistory` stores self-contained canonical MessagePack bodies rather than authority roots or definitions. Fixed admitted indexes bound live transactions and retirement debt; the default limits are 4,096 transactions, 16 MiB of payload capacity and 256 KiB per transaction. Indexes, shared controls and actual buffer capacity use the common physical ledger. Eviction removes coverage immediately, while exported owners keep evicted bodies charged until their final release. Cleanup advances through the owner executor's receipt and byte budgets.

Coverage requires a complete committed revision suffix. Publication independently checks revision continuity; missing capture, allocation refusal, encoding limits, eviction and context replacement force snapshot fallback for older bases. Optional cache pressure never rejects an otherwise valid committed transaction. Completed drafts also budget their sealed-body disposal when cancelled before staging.

## Delta and progress encoding

`DeltaExportEncoder` checks complete base/target identities and history coverage, then pins self-contained bodies from the requested revision suffix. Authorized namespace filtering precedes counting. The encoder emits a delta containing only selected transactions, or progress when the fully covered suffix changes none of the selected namespaces. Loss of coverage during capture refuses export before any chunk is exposed. After capture, cache eviction cannot invalidate pinned bodies.

Counting precedes emission and enforces aggregate wire, mutation and string limits plus per-definition byte and override limits. The cache and scope must share a physical account. Lexical traversal reuses canonical cached bytes while adding the authorized namespace index to mutation tuples; definitions are not copied into a second object model. Output uses the same independently owned 65,536-byte chunk boundary as snapshot export. Cancellation drains pinned bodies, output and the admitted reference index with caller budgets; the encoder retains its scope owner until final disposal.

## Qualification and remaining work

The eight focused authority/codec suites pass on macOS (1.41 seconds), Linux (1.11 seconds) and FreeBSD (1.45 seconds), under ASan/UBSan (6.68 seconds) and fully instrumented TSan (9.78 seconds). Coverage includes frozen export during writes, authorization isolation, canonical hashing, randomized lower-bound traversal, replacement-snapshot retirement, independent MessagePack vectors, multi-chunk output and retained-allocation refusal. Final empty-scope and schema-limit rejection checks also pass separately on all three platforms and under both sanitizers.

The recorder and history suites pass natively (0.31 seconds), under ASan/UBSan (0.55 seconds) and fully instrumented TSan (0.97 seconds). Five native authority regressions, including both Node storage modes, pass in 10.95 seconds. Coverage includes canonical payload bytes, publication visibility, retry filtering, changed-only capture, pinned eviction accounting, missing-revision fallback, optional encoding pressure, and complete-draft cancellation with one-byte disposal budgets. These timings describe shared-host test runs, not performance benchmarks.

Five history/delta/Node suites pass on Linux (0.91 seconds) and FreeBSD (0.69 seconds). The final delta fixture passes natively (0.28 seconds), under ASan/UBSan (0.38 seconds) and TSan (0.76 seconds), on Linux (0.02 seconds) and FreeBSD (0.02 seconds). It checks an independent tuple oracle, the existing semantic decoder, one-byte work budgets, namespace isolation, scoped progress, pinned eviction, pre-emission limits and budgeted cancellation. A 64-transaction workload with 128 rollout overrides per definition crosses multiple output chunks and verifies decoded values. These timings describe shared-host test runs, not performance benchmarks.

These tests establish library behavior. Owner-slot integration, authentic transport settlement and end-to-end authority/agent/SDK qualification remain required before Stage 4 acceptance. No delivery throughput or fleet-capacity claim follows from these fixtures.
