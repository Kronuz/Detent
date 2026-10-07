# Distribution and immutable local publication

**Date:** 2026-10-06.

**Status:** Validated architectural blueprint, accepted for Stage 4 implementation. Whole-component acceptance remains pending.

| Reviewer | Decision | Date |
| --- | --- | --- |
| Lead Software Architect | GO for the codec, completion boundary and complete Stage 4 design | 2026-10-06 |

## Abstract

Stage 4 uses a bounded, narrow MessagePack codec and immutable, dynamically sized local generations. One complete component covers portable snapshot/delta contracts, completion-driven image publication, pinned local views and agent cursor integration. The [codec comparison](DISTRIBUTION_CODEC.md) records the measured CPU and compressed-size tradeoff; the benchmark schemas remain experimental until the supported contract is implemented and qualified.

The [accepted architecture](VALIDATED_ARCHITECTURE.md) and [delivery sequence](IMPLEMENTATION.md#remaining-acceptance-and-delivery) govern the scope. Stage 4 establishes portable delivery and immutable views. Stage 5 adds resumable streams, namespace subscriptions, relay fan-out and incremental page/segment publication; sustained fleet qualification follows those mechanisms.

## Validated Architectural Blueprint

1. **Scalability Check.** The 500,000-knob comparison measures MessagePack encode CPU at 123–130 ms and decode CPU at 621–627 ms. JSON+zstd3 produces the smallest compressed image, so the selected codec trades some wire bytes for less measured encode/decode CPU and a narrow implementation with no new MessagePack runtime dependency. Final framing, direct image construction and daemon memory still require measurement; complete conversions must not run inline on an asynchronous owner.
2. **Edge-Case Analysis.** Exact base matching, complete-candidate validation, atomic descriptor selection and pinned generation ownership prevent partial updates from reaching readers. A failure before selection retains the old root; an uncertain replacement or later durability failure may have selected the complete new root and requires recovery, not a claimed rollback. Accepted originals retain their files, lock, admission and authentic-result obligation through consumer cancellation and shutdown.
3. **Dependency & State Audit.** Extract generic completion IO into the standalone sibling `cluster/completion/` target, preserve canonical journal types through forwarding headers and expose additive completion aliases. Opaque publication owns generation files and descriptor bytes; Detent owns cursor and record semantics. The local SDK's lookup/view target requires neither networking nor the distribution codec, and legacy Xapiand APIs and storage formats remain unchanged.
4. **Security & Scale Review.** Structural, semantic, decompression and aggregate limits precede allocation or publication. Scope identity binds configured authorization and subscription identity but grants no authority itself. Resource accounting covers construction, retained generations, accepted originals and cleanup debt; component acceptance requires malformed-input, publication-fault, held-reader, shutdown and supported-platform evidence with bounded growth under pressure.

## Motivation and alternatives

The existing mutable 4 MiB mapping has a fixed capacity, concurrent plain payload access and readers that retry indefinitely when publication remains odd. Replace it with complete immutable generations, so a reader owns a stable root even when refresh or publication fails.

The alternatives considered are:

1. **Narrow MessagePack, selected.** Use a typed tuple schema and bounded parser rather than importing the broad object model and dependencies of the existing Kronuz/msgpack DOM. The measured candidate preserves every decoded knob and override.
2. **Protobuf Lite.** Its generated candidate uses more encode/decode CPU in the measured corpus. The installed SDK dependency graph does not establish a minimized static footprint, so the decision must not claim that all installed SDK libraries are intrinsic Protobuf requirements.
3. **JSON+zstd.** It produces approximately half the compressed bytes of the binary candidates on this corpus, with higher measured encode/decode CPU. Preserve JSON polling through an explicit compatibility adapter.

Five warm repetitions produce each process's median, and three fresh processes repeat the 500,000-knob comparison. Prototype RSS includes repeated model construction/destruction and does not predict steady-state agent memory. The [measurement artifact](../benchmarks/distribution-codec-comparison.json) and [codec document](DISTRIBUTION_CODEC.md) record the corpus, footprint scope and reproduction instructions; neither establishes production tails, fan-out, cost or power savings.

## Cursor, profile and subscription identity

The distribution contract distinguishes state progress from transport progress:

```text
Cursor = [cluster, group, restore_epoch, scope_id, revision]
Profile = [semantic_profile_id, required_capabilities]
```

The authority Context uses 16-byte `cluster`, `group` and `epoch` identities. Encode each as a fixed-length binary value; `restore_epoch` is the Context's `epoch` field. Revision is an unsigned 64-bit state revision, independently tracked from Raft index, with overflow rejected rather than wrapped.

`scope_id` is a 32-byte identity binding the canonical ordered namespace set and effective authorization identity. Compute it from an explicitly framed, domain-separated canonical representation, without credential secrets or ambiguous concatenated strings. Namespace indexes use that same canonical order; changing scope requires a fresh snapshot.

`semantic_profile_id` binds supported definition and evaluation semantics, including rollout hashing. Required capabilities are an unsigned bit set whose unsupported bits cause rejection. Schema/profile compatibility participates in resume negotiation; changing a transport compression choice does not change the state Cursor.

The controller obtains the expected Context, scope and profile from trusted configuration or authenticated negotiation. Payload assertions do not grant read access. Apply existing namespace/key validation consistently before authorization, subscription identity calculation and lookup; introduce no implicit dotted-name flattening or different normalization rules between those boundaries.

Within one Context and scope, revisions cannot roll backward. A trusted restore/reset explicitly changes restore epoch. A snapshot for an unexpected identity is rejected until the controller authorizes that reset; revision rollback cannot masquerade as ordinary forward progress.

## Narrow MessagePack schema

V1 uses exact tuple shapes. The symbolic tags below are protocol constants with documented values and independent vectors; benchmark tuple shapes do not define the supported protocol.

Update tags are snapshot `1`, delta `2`, and progress `3`. Definition tags are boolean `0` and integer `1`; override tags are forced boolean `0` and percentage `1`; mutation tags are upsert `0` and delete `1`. Semantic profile `1` preserves the existing SHA-256 percentage rollout and switch-override evaluation. Required capability bit `0` identifies support for these definition semantics; unsupported bits are rejected.

| Object | V1 tuple |
| --- | --- |
| Snapshot | `[1, snapshot_tag, profile, target_cursor, counts, namespace_revisions, records]` |
| Delta | `[1, delta_tag, profile, base_cursor, target_cursor, counts, transactions]` |
| Progress | `[1, progress_tag, profile, base_cursor, target_cursor]` |
| Namespace revision | `[namespace, revision]` |
| Snapshot record | `[namespace_index, key, key_revision, definition]` |
| Integer definition | `[int_tag, signed_value]` |
| Boolean definition | `[bool_tag, default_value, rollout_or_nil, overrides]` |
| Forced override | `[switch_value, forced_tag, boolean_value]` |
| Rollout override | `[switch_value, rollout_tag, percentage]` |
| Transaction | `[revision, touched_namespace_revisions, mutations]` |
| Upsert mutation | `[namespace_index, key, upsert_tag, definition]` |
| Delete mutation | `[namespace_index, key, delete_tag, nil]` |

Snapshot `counts` declares `[namespace_count, record_count, override_count, string_bytes]`. Delta `counts` declares `[transaction_count, mutation_count, override_count, string_bytes]`. Check every declared total against actual decoding as well as admission limits; byte totals count the UTF-8 string payloads in that logical object, separately from binary identity fields and framing bytes.

`namespace_revisions` contains the canonical scope's namespaces and their revisions, including subscribed empty namespaces. Delta `touched_namespace_revisions` uses namespace indexes from that scope rather than introducing a different dictionary. A snapshot record's key revision is valid at its target; a transaction's changed keys share that transaction's state revision.

Records and override keys are ordered and unique. Transactions have increasing revisions, and keys are unique within each transaction. Preserve existing definition validation, integer ranges, rollout percentages, rollout hashing and live/default semantics.

V1 exports subscriber configuration and revision metadata. Keep producer sessions, receipts, ballots and recovery-only state in authority-private formats; a private consensus/checkpoint image is not the agent wire schema.

A delta applies only when its entire base Cursor matches the locally published base and its profile is compatible. A mismatch requests resynchronization without modifying the current root. Snapshot transfer pins revision `R`; subsequent changes begin strictly after `R`.

The initial decoder requires forward target progress and at least one nonempty transaction for a delta. Transactions have increasing revisions strictly above base and at or below target; gaps and irrelevant trailing progress are allowed. Within each transaction, mutations are ordered uniquely by namespace index and key. Touched namespace names are ordered and unique, each watermark equals the transaction revision, and every touched namespace has a mutation. Deletes carry `nil`. The dedicated progress shape carries no mutations or namespace changes and advances only the global cursor.

The in-memory delta stores touched rows and mutations in global deques. A transaction descriptor contains absolute start/count ranges and is appended only after the entire transaction validates. This replaces the initial per-transaction deques without changing wire tuples. The unreleased C++ API now exposes checked `mutation(ordinal)` and `touched(ordinal)` access rather than nested containers; consumers keep the sealed delta alive while borrowing its rows. Disposal removes rows under work budgets before dropping descriptors. Current storage does not pop from the front, so absolute ordinals start at zero; future paged retention must preserve a checked base ordinal.

The same valid 100,000 one-delete transactions occupy 2,937,644 wire bytes. In fresh macOS Release processes, the original decoder peaks at 843,522,048 RSS bytes versus 21,524,480 after flattening; decode wall time is 442,804 versus 49,968 microseconds, with identical 9,376 budgeted turns. [The measurement artifact](../benchmarks/delta-layout-measurements.json) records smaller cases and scope. Build `detent_delta_decode_bench` and run it with the transaction count to reproduce the current decoder workload. These samples exclude final disposal and establish neither service tails nor fleet capacity; physical allocation accounting and cleanup qualification remain required.

A progress message advances across transactions irrelevant to the selected scope. Applying several transactions into one candidate may expose their final target state, but every visible generation represents complete transactions. Preserve encoded transaction boundaries so the state and atomicity oracle can verify the projection.

Encoders produce a documented canonical representation, including integer/length forms and ordering. Same-Cursor idempotence compares validated state identity rather than arbitrary compressed bytes. Existing authoritative request digests remain semantic and separate from distribution serialization.

## Framing and bounded parsing

Use an explicitly encoded fixed prefix containing magic, framing version, kind/flags, encoded length, decoded length, update identity, logical offset, total logical bytes and checksum. Document byte order and offsets; native structure packing is not a wire contract. Bind framing kind to the decoded logical object.

The implemented initial framing profile uses a 96-byte header. All multibyte integers are big-endian. SHA-256 covers the first 64 header bytes followed by the encoded fragment payload; this detects corruption and does not authenticate the sender. Decoded fragments are at most 65,536 bytes; encoded fragments are bounded by `ZSTD_compressBound(65536)`. Compression requires explicit caller negotiation.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | ASCII `DTDIST01` |
| 8 | 1 | Framing version `1` |
| 9 | 1 | Snapshot `1`, delta `2`, progress `3` |
| 10 | 2 | Flags: zstd `1`, final fragment `2` |
| 12 | 4 | Encoded payload length |
| 16 | 4 | Decoded payload length |
| 20 | 4 | Reserved, zero |
| 24 | 16 | Nonzero update identity |
| 40 | 8 | Logical decoded offset |
| 48 | 8 | Total logical decoded length |
| 56 | 8 | Reserved, zero |
| 64 | 32 | SHA-256 checksum |

`FragmentDecoder` admits one update and one output fragment at a time, checks sequential offsets and stable identity/total, and stops consuming input while the output slot is occupied. Header copying and payload hashing consume the caller's byte budget; header validation and completed-fragment decoding consume token units. Decompression handles only one bounded fragment per unit, never an entire snapshot. The caller drains the complete supplied input, forwards each decoded fragment to the semantic decoder, and calls both finalizers before preparing a generation.

Require sequential offsets for one admitted candidate and exact logical completion at the final fragment. Reject truncated final input, trailing objects/bytes, inconsistent identities or totals and undeclared flag meanings. Duplicate/reconnect handling in Stage 5 sits above this parser and must not silently append the same fragment twice.

Use bounded fragments, with 64 KiB decoded fragments as the initial profile. An individual definition may span fragments; record limits must not accidentally make a legal large definition impossible to transfer. Optional zstd compression is negotiated and bounded per fragment, while raw MessagePack remains supported.

Counting, encoding, decoding, semantic validation and image construction expose byte/record work budgets. Every variable-length field can continue across calls without retaining an unbounded input buffer. A caller owning an asynchronous service or stream executor must never run a complete 500,000-record conversion inline.

The decoder accepts only the needed array, integer, string, fixed identity-binary, boolean and nil forms. Reject maps, floats, extension values, wrong tuple arity, unknown operation/type tags, unsupported required capabilities, invalid identity lengths, malformed text, duplicates, invalid ordering and out-of-range values.

Check counts, lengths, arithmetic and projected construction/storage limits before allocation. Bound wire bytes, decompressed bytes, nesting, strings, records, overrides, total live state, output image size and admitted candidates independently. Whole-image compression results must be repeated with actual framing and decompression.

## Reusable completion boundary

Physically locate the generic primitive IO, IOOperation, POSIX backend and related completion headers in sibling `cluster/completion/`, with its own standalone CMake target. Its core has no journal, consensus, reactor or FetchContent dependency. Preserve the existing canonical journal type identities and public headers through relative forwarding, and add clean `kronuz::completion` aliases rather than duplicating definitions.

Agent-only FetchContent uses `SOURCE_SUBDIR completion`, avoiding root cluster networking configuration. Full builds already using cluster share the same target. Keep the existing Kronuz/io wrapper unchanged; this packaging can later be extracted without another state-machine rewrite.

Optional native Asio adapters remain separate from the dependency-free completion core. Drive the same operation states synchronously on the existing bounded poll/build worker and asynchronously through native adapters. Create/Open/Size and other metadata fallback work remain on the bounded fallback executor, while supported native primitives use their existing drivers.

Add opaque-image preparation/publication jobs over those operations. The generic module understands files, generation identity, descriptor bytes, integrity, admission and ownership; Detent interprets Cursor, profile and record semantics. Avoid a second POSIX backend or an interim publisher API that blocks asynchronous owners.

Accepted jobs retain their backend, files, owner lock, reservation and authentic-result obligation until genuine settlement. Consumer cancellation stops interest; it cannot fabricate completion, refund live debt or delete an artifact beneath an original operation. Closing rejects new admissions/jobs, reaps accepted originals and then performs bounded disposal.

The created-artifact permission option defaults to `0600`; the configured system-agent profile may use `0644` for image/descriptor artifacts. Keep `owner.lock` at `0600`, reject unsupported permission bits and report successful Create only after authentic permission-setting success. Preserve confinement, no-follow opening, exclusive generation creation and a single publisher owner.

## Immutable image, descriptor and leases

Publish uniquely named, dynamically sized image files through an atomically replaced descriptor. The descriptor binds generation identity, exact image length, integrity metadata, local layout version and bounded opaque Detent metadata containing Cursor/profile. Do not reuse or overwrite a generation name.

The local image contains a versioned header, namespace directory, indexed records, overrides and string regions. Use explicit integer widths and checked offsets/counts. Validate every region before exposing its mapping; neither STL objects nor process pointers belong in the file.

Construct directly from validated records where practical rather than requiring another complete Snapshot copy. Count the final layout before reserving storage. Never truncate or overwrite a published generation, and do not retain a mutable seqlock payload in the replacement format.

A mapped root owns every resource required by its views. SDK refresh opens and validates a candidate on its worker, then atomically installs the fully pinned root. Read-view acquisition uses the already available root and does no explicit filesystem, network or consensus work.

Enforce retirement accounting across cooperative SDK processes with a generation-specific shared kernel lease acquired before opening the image. Retain that lease through the mapped root's final view. GC acquires the same lease exclusively without blocking before unlinking, so an image-open original cannot outlive accounting without a lease.

Use a separate generation lease file to close the open/reclamation race. A reader checks the lease's identity and link state after acquisition and before opening the image; an unlinked lease or descriptor race causes a bounded retry before the view is exposed. Metadata/lease operations use the worker or native adapter, never blocking an asynchronous owner.

Retired generations remain charged while leases prevent reclamation. Bound named retirement, builds, abandoned files and refresh/open attempts. Reader-held generations may cause publication pressure; preserve the last usable root and report that pressure rather than accumulating images indefinitely.

Reclaim quota only after actual removal and the required namespace barrier. Keep possibly selected generations protected during uncertain publication. Supported OS mapping/unlink and lock-lifetime behavior require qualification, including process death; uncooperative local processes lie outside the SDK lease contract.

## Publication results and cursor transitions

```text
Received → Validated → Image prepared → Locally published → Checkpointed
```

Track all five states independently. Parsing or validation alone cannot advance a published or recoverable Cursor. Preparation yields an unforgeable `PreparedImage` only after complete validation, construction and authentic sealing results; publication rechecks the expected base generation before selection.

Runtime descriptor selection advances the locally published Cursor. Optional persistent checkpoint barriers advance the checkpointed Cursor only when its selected image is recoverable; persisting a Cursor without that image is insufficient. Runtime mappings alone do not provide reboot recovery.

Before descriptor selection, failure retains the previous published root. An uncertain descriptor replacement or later durability failure may have selected the complete new root; preserve both possible generations, report uncertainty and recover the selected descriptor. Do not claim rollback or delete an artifact that might be current.

After uncertain descriptor mutation, fence further publication until selected-state recovery resolves the outcome. Keep ordinary reads on a complete retained root. Accepted publication completes its required original barriers even if the consumer stops waiting, and recovery decides which durable generation is selected.

Same-Cursor/different-state input is a conflict or corruption outcome. A trusted restore/reset changes epoch before a lower revision can become current. The image builder and publication capability bind the validated scope/profile/target to the selected generation so callbacks cannot substitute an unrelated image.

## Public API and integration

| API | Obligation |
| --- | --- |
| `SnapshotDecoder` / `DeltaDecoder` | Incremental validated input with explicit limits and expected identity/base |
| `ValidatedUpdate` | Private construction binding scope, profile, target and optional base |
| `ImageBuilder` | Bounded layout/build work and admitted preparation jobs |
| `PreparedImage` | Sealed image ownership and publication capability |
| `ImagePublisher::begin_publish(...)` | Expected-generation check and completion-driven result |
| `GenerationReader::refresh(...)` | Worker-side open, pin, validation and root installation |
| `ReadView` | Stable Cursor and consistent namespace/key lookup |
| `Client::read_view()` | Additive scoped API; existing calls can acquire a view per call |

Keep the publication mechanism independent from Detent definitions and consensus. A prepared generic image carries bounded opaque descriptor metadata, while the Detent layer validates that metadata and its mapped record layout. Native SDK read/view targets require neither the codec runtime nor networking.

New multi-namespace lookup uses an explicit namespace/key boundary. Existing single-name calls bind to a configured namespace rather than silently flattening ambiguous names. Preserve baked defaults, CLI overrides, rollout evaluation, type/conversion errors and deletion fallback.

Give the agent its own candidate/publication pipeline. The existing post-acceptance `Client::on_update` notification cannot authorize the new published/recoverable Cursor because Client acceptance precedes shared-memory publication. Retain JSON polling through an explicit legacy adapter rather than treating its version-only patch contract as a strict Cursor.

Preserve the previous root on decode, build or pre-selection publication failure. While a candidate is admitted, bound subsequent input and apply backpressure; do not queue an unlimited succession of parsed snapshots. Health distinguishes source connectivity, validated progress, published progress, checkpointed progress, retention pressure and last publication failure.

Stage 4 may build complete immutable generations. Its ownership and descriptor contract accommodates Stage 5 indexed pages or segments, path copying and transitive pinning without changing the application consistency contract. Whole-generation Stage 4 evidence cannot establish the target sustained update rate.

## Whole-component acceptance gates

Implementation proceeds autonomously within the accepted component. Final Architect review covers the extraction, codec, publication jobs, mapping/views and agent integration together; no per-commit approval cycle is required.

1. **Interoperability and semantic oracle.** Independent Python vectors and an independent decoded-value oracle cover empty scopes, integer extremes, revision extremes, large overrides crossing fragments, deletion/default fallback, rollout behavior and namespace separation. Cross-version tests reject unsupported required semantics and verify every supported tuple/tag.
2. **Malformed input and bounds.** Exercise all field types, identity lengths, count/length overflows, duplicate keys, ordering violations, invalid indexes, unknown tags/capabilities, truncation, trailing bytes and decompression limits. No rejected candidate changes the published root or published/checkpointed Cursor.
3. **Authority-to-view integration.** Export a frozen authority revision through the codec, agent candidate, immutable image and actual pinned read views. Compare snapshots and exact-base deltas against authoritative state, including irrelevant-transaction progress and several complete transactions in one publication.
4. **Reset and stale state.** Verify exact-base rejection, scope changes, same-Cursor conflicts, explicit restore epochs and repeated delivery. A revision rollback without an authorized reset cannot replace the current generation.
5. **Publication and original IO.** Inject failures at creation, permission setting, writes, sealing, descriptor replacement and every required directory barrier. Hold accepted originals across cancellation/close; check their files, lease, lock and admission until authentic completion, including uncertain outcomes.
6. **Crash/recovery and leases.** Kill processes around publication, reopen exact selected generations, retain old/new readers concurrently and race lease acquisition with GC. Process death releases reader leases correctly, and possibly selected artifacts survive uncertain publication.
7. **Compatibility and supported builds.** Qualify standalone completion, unchanged journal include paths/canonical types, existing consumers, agent-only configuration and the SDK dependency boundary. Run macOS, Linux and FreeBSD gates with native memory/concurrency instrumentation and the relevant sanitizer profiles.
8. **Capacity and pressure.** Measure final framing/compression, direct image construction, lookup CPU, refresh/publication latency, dependency footprint, peak construction memory and retained generation bytes on the representative corpus. Sustained refresh, held readers, shutdown and cleanup must show bounded queues/debt or explicit admission pressure.
9. **Remaining system gates.** Keep the authority's pressure/churn/telemetry gates in [readiness](AUTHORITY_READINESS.md) explicit until qualified. Stage 5 must still prove retained-history handoff, slow-client resume/reset, relay sharing and incremental publication before Stage 6 fleet/recovery claims.

## Implementation sequence

1. Complete the standalone completion extraction and opaque-image jobs with original-settlement and compatibility tests.
2. Implement the supported schema and incremental codec with independent vectors, then connect validated input directly to the image builder.
3. Add immutable descriptors, generation leases and pinned SDK views, followed by the agent's publication/cursor pipeline and explicit legacy adapter.
4. Run the complete fault, interoperability, platform and pressure gates, record measurements with their limits, and obtain one consolidated Stage 4 review.

The implemented local image layout, validation and generation lease boundary are documented in [Local image format](LOCAL_IMAGE_FORMAT.md). Publication and agent integration remain part of the same Stage 4 component.

Decoded snapshots and deltas now use charged stable pages, flat wire definitions and global override rows. The [wire storage reference and paired measurements](LOCAL_IMAGE_FORMAT.md#decoded-wire-ownership-and-measurements) describe the unreleased C++ type changes and the remaining aggregate-admission and retirement gates. This is a row-storage subcomponent, not Stage 4 acceptance.
