# Authorized authority export

**Status:** Scope authorization, frozen projection, incremental snapshot/delta/progress encoding, bounded applied-change history and owned frame encoding are implemented. The authority exposes authenticated `/v2/distribution` through the sibling HTTP library's owned response streaming. Focused endpoint qualification passes on all three supported platforms and under sanitizers; managed agent and SDK integration remain pending. The [delivery blueprint](DELIVERY_INTEGRATION_ARCHITECTURE.md) governs the assembled component and its final review.

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

## Owned frame encoding

`OwnedFragmentEncoder` wraps independently owned export chunks in the existing 96-byte framing header. Raw frames match the original encoder byte for byte. Compressed frames use a preadmitted, aligned static zstd workspace and interoperable per-fragment compression. SHA-256 covers the canonical header and actual payload. Only initialized payload bytes are sealed for transport; unused output capacity remains charged.

Construction admits controls, output capacity and compression workspace through the common ledger. Input must be sealed and share that ledger. Taking a frame transfers immutable ownership; a held network frame remains charged across encoder cancellation. Disposal advances through byte and token budgets. Compression itself is one bounded operation over at most 65,536 input bytes after accumulating work credit, rather than an interruptible codec operation.

The sender and existing framing suites pass natively (0.32 seconds), under ASan/UBSan (0.92 seconds) and fully instrumented TSan (2.43 seconds), on Linux (0.12 seconds) and FreeBSD (0.09 seconds). Coverage includes all update kinds, raw byte equivalence, zstd round trips, empty final frames, invalid input, one-byte work budgets, cancellation and allocation-refusal rollback. These shared-host timings are qualification evidence, not throughput measurements.

## Authenticated delivery endpoint

`POST /v2/distribution` requires the existing bearer credential and configured authority identity. Its JSON request contains `cluster`, `group`, `epoch`, `scope`, `namespaces`, `profile` and `coding`. Authority identifiers use 32 lowercase hexadecimal characters; `scope` uses 64. The supported profile is `1`; coding is explicitly `raw` or `zstd`. An optional `base` is either null or a complete cursor object with `cluster`, `group`, `epoch`, `scope` and `revision`.

The service authorizes every requested namespace before admitting an owned response. Empty selection is an empty scope. The request's scope identity must match the configured principal, policy epoch and canonical namespace selection. Credentials can specify optional `principal` and `policy_epoch` fields; existing credentials default to their producer identity and epoch `1`. Change the policy epoch when grants change. Token rotation alone can preserve the principal and epoch.

After a quorum read succeeds, one authority-owner delivery slot captures the public projection. Exact history coverage selects delta or progress; unavailable coverage selects a snapshot. Equal cursors also receive a snapshot, allowing the receiver to check canonical state identity. The slot stays occupied through encoder disposal, projection retirement and authentic network settlement. Waiting requests retain bounded metadata rather than additional authority roots.

A successful response has media type `application/vnd.detent.distribution` and bounded HTTP chunked framing. `X-Detent-Profile` is `1`, `X-Detent-Coding` echoes the selected coding, `X-Detent-Kind` identifies snapshot (`1`), delta (`2`) or progress (`3`), and `X-Detent-Revision` gives the frozen target revision. The body contains the established distribution frames; HTTP chunk boundaries do not define distribution fragments. Coding applies to individual fragments rather than HTTP content encoding.

The service shares one physical ledger across scope storage, history, export controls, frame buffers, codec workspace and owned HTTP response controls. Optional `delivery_limits.heap_bytes` configures its capacity (default 128 MiB); `delivery_limits.wire_bytes` bounds an encoded update (default 256 MiB). Both are capped at 1 GiB, and heap capacity must be at least 1 MiB. Authority nodes, networking runtime internals and whole-process RSS remain outside this ledger.

Startup waits for committed application replay before admitting requests and attaching delivery history. Storage readiness alone is insufficient because replay advances incrementally. Consequently, restart preserves durable authority state while invalidating the old in-memory change suffix. Election follows its existing lifecycle; recovered storage does not by itself make the node a leader.

## Incremental client reception

The `detent::delivery_http` library parses HTTP incrementally into a fixed 4 KiB body slot, validates response metadata, and passes distribution fragments directly to the semantic decoders. It accepts bounded content-length or chunked responses, rejects duplicate headers and trailers, requires exact profile/coding agreement, and verifies the declared target revision against the decoded update. Truncated responses and trailing bytes cannot expose a validated candidate. Header storage, decoder controls, payloads and codec workspace use the shared physical allocation ledger; retained results keep their charges after parser disposal.

The separate `detent::delivery_transport` library uses Asio coroutines for TLS connect, handshake, writes and reads. One absolute deadline covers the request and response. Cancellation waits for original network operations and budgeted candidate disposal before returning. Certificate files are loaded when preparing the TLS context on the startup/configuration worker; runtime reception uses the existing chain and hostname verification code extracted into `detent_transport/tls.hh`. The authority's previous TLS header retains its public names and peer-specific validation. Asio/OpenSSL internal allocations and caller-owned configuration strings remain outside the delivery ledger.

These libraries require neither Raft nor the full HTTP server. `DETENT_BUILD_MANAGED_AGENT` enables the delivery support targets; production agent integration is still pending. The reception fixtures cover raw/zstd snapshot, delta and progress delivery, fragmented HTTP input, malformed metadata, truncated frames, checksum failure, TLS hostname rejection, deadlines and a 10,000-record snapshot. A real authenticated authority also delivers snapshots through the coroutine client. The eight focused codec/client/service tests pass natively (4.13 seconds), on Linux (4.40 seconds) and FreeBSD (3.53 seconds). The same eight suites pass under ASan/UBSan (4.71 seconds) and fully instrumented TSan (5.99 seconds). These timings are test evidence rather than throughput measurements.

## Qualification and remaining work

The eight focused authority/codec suites pass on macOS (1.41 seconds), Linux (1.11 seconds) and FreeBSD (1.45 seconds), under ASan/UBSan (6.68 seconds) and fully instrumented TSan (9.78 seconds). Coverage includes frozen export during writes, authorization isolation, canonical hashing, randomized lower-bound traversal, replacement-snapshot retirement, independent MessagePack vectors, multi-chunk output and retained-allocation refusal. Final empty-scope and schema-limit rejection checks also pass separately on all three platforms and under both sanitizers.

The recorder and history suites pass natively (0.31 seconds), under ASan/UBSan (0.55 seconds) and fully instrumented TSan (0.97 seconds). Five native authority regressions, including both Node storage modes, pass in 10.95 seconds. Coverage includes canonical payload bytes, publication visibility, retry filtering, changed-only capture, pinned eviction accounting, missing-revision fallback, optional encoding pressure, and complete-draft cancellation with one-byte disposal budgets. These timings describe shared-host test runs, not performance benchmarks.

Five history/delta/Node suites pass on Linux (0.91 seconds) and FreeBSD (0.69 seconds). The final delta fixture passes natively (0.28 seconds), under ASan/UBSan (0.38 seconds) and TSan (0.76 seconds), on Linux (0.02 seconds) and FreeBSD (0.02 seconds). It checks an independent tuple oracle, the existing semantic decoder, one-byte work budgets, namespace isolation, scoped progress, pinned eviction, pre-emission limits and budgeted cancellation. A 64-transaction workload with 128 rollout overrides per definition crosses multiple output chunks and verifies decoded values. These timings describe shared-host test runs, not performance benchmarks.

The service regression, owner export and real TLS endpoint suites pass natively (10.37 seconds), under ASan/UBSan (11.47 seconds) and fully instrumented TSan (14.39 seconds), on Linux (9.68 seconds) and FreeBSD (9.47 seconds). The TLS fixture checks raw/zstd snapshots, scoped delta and progress, equal-cursor snapshots, duplicate-field rejection and snapshot fallback after actual service restart. It verifies the TLS chain and hostname and decodes the resulting framed payload through the semantic decoders.

The separate held-original fixture passes natively (0.92 seconds), under ASan/UBSan (1.03 seconds) and TSan (1.29 seconds), on Linux (0.29 seconds) and FreeBSD (0.31 seconds). Four admitted delivery requests wait behind one held original, while an ordinary quorum read progresses. Shutdown cannot complete or release the frame/request retention until the original is reaped. These are focused ownership gates; representative capacity, broader endpoint pressure and the complete authority/agent/SDK path remain open. No delivery throughput or fleet-capacity claim follows from these fixtures.
