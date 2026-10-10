# Shared durable distribution relays

**Date:** 2026-10-10
**Status:** Validated Architectural Blueprint, Stage 5B. Lead Software Architect review at max reasoning effort gives design GO. Implementation follows this component plan, with one consolidated review after qualification.

## Purpose and boundary

A relay subscribes once for each configured distribution bundle, durably publishes its public generation, and shares independently owned encoded snapshots and update edges among authorized downstream agents. It is a trusted non-voting service. It uses the existing session protocol and complete-image publication; immutable incremental publication follows in Stage 5C.

```text
Authority session → validate/build → durable selection → recover
                          ↓                                  ↓
                    staged update                     pinned public image
                          └──────── shared encoded cache ────┘
                                            ↓
                                authenticated agent sessions
```

| Reviewer | Verdict | Boundary |
| --- | --- | --- |
| Lead Software Architect, max effort | Design GO | Complete Stage 5B component; source and qualification review at completion |

## Identity and authorization

Configuration supplies a finite set of bundles. Each binds the full authority Context, effective principal, policy epoch, canonical namespace set and semantic profile, with one upstream subscription and one durable public generation. An untrusted Open cannot create a bundle. Sharing requires exact bundle identity; different principals or namespace sets use separate admitted bundles, without rewriting identities across principals.

Downstream credentials authenticate locally and authorize the entire selected bundle on every request. Upstream credentials are read-only and namespace-limited. Startup readiness requires a fresh authenticated upstream Open. Explicit Unauthorized, Forbidden or IdentityChanged fences that bundle and revokes new delivery. Transient disconnection may retain the documented stale-read contract for established authorized data. A payload cannot authorize an epoch or scope transition.

An agent whose actual durable base is ahead of the relay receives a bounded transient `source_behind` control, HTTP 503 with an additive typed client reason. Its selected image remains intact. The relay sends no backward snapshot and makes no quorum-freshness assertion through Open or idle responses.

## Reusable components and publication

1. Extract transport-free publication-window, scope/admission and lifecycle mechanisms. Authority history and relay cache supply adapters for current cursor and continuity. Consensus and authority State remain outside the relay core. Preserve existing interfaces through adapters; the SDK reader gains no networking, codec, relay or consensus dependencies.
2. Encode canonical profile-1 objects from sealed validated updates and `PinnedImageSource`. Snapshot export reads public definitions and namespace/key revisions without legacy snapshots or SDK defaults/override overlays. Each raw/zstd representation is built once per shared object. Workspace, indexes, pages and retirement consume admitted physical capacity; unavailable streaming support fails explicitly.
3. Add an optional coordinator publication sink. It stages independent sealed cache bytes while validated input remains alive, then exposes them only after reconciliation confirms matching selected and recovered generations. References to validated inputs do not supply cache ownership because their discard cursors consume storage. Direct agents retain existing behavior.
4. Send upstream feedback after matching durable relay selection and recovery, independently of downstream acknowledgments. Cancellation after selection requires reconciliation before cache visibility; an ambiguous upstream session remains invalidated.

## Retention and downstream windows

Cache objects bind complete identity/profile and exact base/target cursors. A bounded chain has entry, physical-byte, age, staging and retirement limits. Resume initially requires retained update boundaries; an interior base may require a snapshot. Progress requires verified complete coverage. Eviction invalidates advertised coverage immediately while pinned objects remain charged through final release.

Optional history may be dropped under pressure while durable publication continues, with an explicit coverage gap. Sessions retain continuity metadata after transport settlement without private history queues. Restart recovers the selected generation, changes session/cache incarnation and rebuilds snapshots; old sessions expire. Forced-snapshot retries must obtain a usable current-publication snapshot rather than repeatedly serving an obsolete object without continuation.

Existing Open/Next/Close routes, headers and frames remain compatible. Each client has one outstanding offer, a fragment position and one accepted-write obligation. Advance requires exact durable feedback and authentic prior stream settlement. Duplicate accepted feedback is idempotent; missing, skipped or conflicting feedback fails explicitly. History loss closes the session and requires forced snapshot reopening.

## Admission, scheduling and host

Session, parked-poll, control, bootstrap, snapshot-transfer and catch-up capacity have distinct configured limits. Listener intake preserves controls before authentication. Bounded round-robin work, limited encoding/recovery jobs, monotonic transfer deadlines and jittered upstream reconnects prevent one bundle or transfer from monopolizing the owner. Shared workers and retirement infrastructure have admitted controls.

One physical heap ceiling covers live, staged, pinned and retired cache/control storage. Mapping and disk accounts remain separate. TLS/framework/thread/kernel costs are recorded separately. Shutdown revokes new work, drains accepted network/storage originals and workers, then releases cache, mappings, leases and accounts.

Deliver an optional transport-free relay library and service target with `detent-relay`, private bounded configuration, TLS preparation, durable-root provisioning, readiness, status and health. Existing agent subscription configuration points at the relay endpoint, hostname and CA. Preserve direct authority delivery and named legacy modes. Metrics cover received/selected/recoverable cursors, cache coverage and debt, active transfers, resets, publication failures and scheduling, with bounded labels.

## Qualification

| Gate | Required evidence |
| --- | --- |
| Semantics | Independent direct-agent/relay-agent equivalence for snapshot, delta, progress, deletion/default fallback, repeated keys, untouched revisions, empty scope and same-cursor conflicts |
| Publication | No upstream feedback or cache visibility before matching selection/recovery; cancellation after selection; crash/restart and uncertain-storage fencing |
| Retention | Eviction with slow clients, exact-edge resume, interior-base reset, forced-snapshot retry, gaps during gathering/before feedback and stale incarnations |
| Security | TLS hostname/chain, principal/policy/grant rotation, namespace isolation, read-only upstream access and malformed input |
| Ownership | Saturation/refusal, held header/frame originals, facade loss, weak controls, pinned generations, shutdown and final physical-account release |
| Fan-out | Real TLS authority → relay → managed agents → separate readers; 100,000/500,000-record states, multiple bundles and at least 500 same-bundle subscribers |
| Measurement | Shared encoded payload against subscriber count; added metadata, upstream export/encoding counts, CPU, egress, mappings, disk debt and scheduling latency |
| Compatibility | Full native regression, Linux/FreeBSD, ASan/UBSan, assembled TSan, relay-only/authority-disabled, agent-only, reader-only and legacy/sibling profiles |
| Review | One consolidated scalability, edge-case, dependency/state and security/scale review |

Stage 5C adds immutable page/segment publication with an explicit format/migration boundary and measurements of reduced rewrite work. Stage 6 qualifies sustained changed-update ingestion, bootstrap/reconnect storms, multi-host failure domains, audit/rollback and pinned dependency reproduction. Stage 5B retains complete-image costs and makes no fleet-capacity claim.

## Implementation evidence

The client recognizes HTTP 503 `source_behind` as `SessionResetReason::SourceBehind`, preserving a live session and its accepted publication cursor. Open reports the same typed reason for retry without requesting a backward snapshot. The scripted real TLS lifecycle verifies a lag response followed by another Next carrying the same accepted window and cursor, then a true expiry that invalidates the handle. Raw and compressed delivery variants pass natively (1.13 seconds including certificate preparation) and under TSan (1.49 seconds). Relay hosting and shared-cache implementation remain ahead.

`PublicationWindow<Marker>` now supplies the common one-offer state used by authority sessions. It depends only on cursor types and standard headers; history markers remain adapter-specific. Feedback validation, original-transfer settlement and accepted-cursor advancement are separate operations. The authority adapter retains authenticated request tokens, incarnation checks, expiry, history coverage and account-backed slot ownership. The reusable state supports move-only continuity markers with nonthrowing moves; a standalone compilation checks that contract without authority or transport headers.

After extraction, the authority table and original-ownership fixtures pass natively (1.65 seconds) and under TSan (2.27 seconds). The five native publisher/idle/held-header/scope checks pass in 64.00 seconds, including 100,000/500,000-record idle states. Three additional TSan publisher/held-header/scope checks pass in 38.95 seconds. All five corresponding ASan/UBSan table and ownership checks pass in 22.90 seconds. These qualify reuse of the existing authority behavior; they do not qualify a relay implementation.

`SnapshotSource` and `SnapshotEncoder` share canonical count/emission between authority captures, validated snapshots and pinned public images. Authority `SnapshotExportEncoder` retains its existing API as a thin adapter. The optional `detent::local_export` target supplies the pinned adapter and leaves reader-only dependencies unchanged. Adapters and encoder controls use their configured physical accounts. Every source step is bounded, including empty authority namespaces; only sealed chunks leave the encoder.

The independent authority wire oracle remains unchanged. Validated-input and pinned-image encoders reproduce the local fixture byte for byte, including namespace/key revisions, minimum signed integer, boolean rollout and group rollout. Pinned encoding with a one-byte/one-token budget returns heap use to its starting resident value while retaining the existing mapping. The three encoder/adapter fixtures pass natively (1.02 seconds), under ASan/UBSan (1.69 seconds) and TSan (3.47 seconds). Eight assembled native authority checks pass in 54.55 seconds, including real TLS, retained originals, publisher pressure and both large-state idle fixtures.

`FramePump` shares the single-chunk/single-frame lifecycle around `OwnedFragmentEncoder`, including exact final-fragment selection and bounded disposal. Authority `DeliveryExport` uses it and retains source/capture retirement and authentic HTTP settlement separately. Raw/zstd multi-fragment tests reject a second input before the prior frame is taken, retain output through pump disposal, decode the complete logical payload and cover an empty terminal frame. Framing and authority-export checks pass natively (0.56 seconds), under ASan/UBSan (1.58 seconds) and TSan (4.51 seconds).

`DeltaSource` and `DeltaEncoder` now share canonical delta/progress count and emission. Authority history retains its existing `DeltaExportEncoder` interface and reuses cached canonical definition bytes; validated input supplies bounded transaction, mutation and override steps. Snapshot and validated-delta sources share definition writers. Independent fixtures reproduce delta, progress, forced boolean overrides and group rollouts byte for byte with a one-byte/one-token budget, and account usage returns to its resident baseline after disposal.

Four adapter/export fixtures pass natively (1.32 seconds), under ASan/UBSan (2.02 seconds) and TSan (4.10 seconds). The additional group-rollout fixture passes natively (0.29 seconds), under ASan/UBSan (0.36 seconds) and TSan (1.61 seconds). These checks qualify the reusable encoders; relay publication and hosting remain under implementation.

`StorageLedger` supports immutable parent accounts with at most eight levels. Admission charges each ancestor and refunds partial charges on refusal; tickets retain their existing 16-byte layout and allocate no control storage. The leaf must still outlive its tickets. A relay host must classify allocations into bounded cache, publication, control and baseline/retirement children whose ceilings sum to at most the root ceiling, including publication capacity for every configured bundle. Direct root allocations would defeat this reservation rule. Account usage reports admitted physical capacities; peaks can conservatively include overlapping admission attempts.

Validated snapshot/delta factories accept an optional output account, preserving existing calls. Adapter controls, encoder workspace and output use that account while borrowed input remains charged to its original account. Cache sealing must release source borrows before coordinator input disposal; account separation alone does not prevent lifetime starvation. Consumers rebuild for the ledger layout change (24 to 48 bytes on the qualification host).

Hierarchy and separate-output adapter checks pass natively (0.82 seconds), under ASan/UBSan (0.90 seconds) and TSan (1.70 seconds). Cases cover parent refusal rollback, depth limits, ticket moves, weak-control retention, eight concurrent workers and zero final usage. A five-round, ten-million-operation admission/reset microbenchmark measures median 12.26 ns for the prior standalone ledger, 13.43 ns for the new root and 20.95 ns for a child plus root. This measures uncontended accounting overhead on the Intel macOS host, not application throughput; source and results are retained in `.scratch/relay-admission/`.

`UpdateStage` encodes snapshot, delta or progress input once into raw and zstd frame directories. Sealing releases semantic sources and codec workspace before `EncodedUpdate` exposes independently owned frames. Both stage and object use retirement handles; abandoned stages and evicted objects clean up on the fixed worker, one owned frame or input page at a time. The admitted fragment directory has an explicit 65,536-entry ceiling. A held transport original retains its own physical charge after the object retires.

Shared validated-input factories remain borrowing adapters: their caller retains backing input through staging and performs explicit disposal afterward. Move-only overloads transfer input into an admitted private retirement node funded by its original account, independently of the output account. Account mismatch or node admission failure preserves the caller's input; adapter allocation failure queues cleanup of admitted transferred input. No `use_count()` check authorizes mutation of immutable shared storage. Sealing releases source references; transferred-input charges can remain as retirement debt until worker cleanup settles.

`UpdateCache` holds a configured bounded exact-edge ring for one bundle. It resumes only from a retained edge's exact base, reports source lag separately, and invalidates advertised coverage immediately on a publication gap or fence. Stale entries retire incrementally; pinned offers remain usable after eviction. Forced snapshots require the actual selected head, so an obsolete snapshot cannot create a reset loop. Incarnation/lineage markers detect restart and coverage changes. The host still must bind selection to full recovered descriptor and directory identity; this cursor-only primitive does not establish durable publication.

Four source/staging fixtures pass natively; the assembled sanitizer sets pass under ASan/UBSan (1.94 seconds) and TSan (5.08 seconds). The expanded native staging/cache fixture passes in 0.44 seconds. Checks independently decode multi-fragment raw/zstd snapshots, deltas and progress, exercise interrupted staging, retain a readable shared input through weak acquisition, verify ownership refusal and final account release, and cover eviction with a pinned offer, interior-base resets, source lag, publication gaps and fencing. Relay HTTP hosting, durable coordinator attachment and fan-out qualification remain ahead.

The coordinator accepts a default-null `PublicationSink`. Its bounded staging loop runs while validated input is still owned; release/abort completes before builder and input disposal. The attachment receives the full candidate descriptor and store directory identity, then sees selection only through matching recovered generation validation. A same-cursor no-change candidate binds the actual selected descriptor after existing content checks. Cancellation after selection still reconciles the durable head; upstream feedback remains withheld for the cancelled session. Successful upstream Open notifies authentication, and close or explicit terminal source reset fences the attachment before further delivery.

The recording attachment verifies released input borrows before selection, full descriptor/directory binding, fresh subscription authentication and cancellation at the actual selection boundary. All twelve native coordinator cases plus certificate preparation pass in 15.16 seconds, including both large-state profiles and legacy null-attachment behavior. The four focused baseline/attachment cases plus certificate preparation pass under ASan/UBSan (2.13 seconds) and TSan (11.09 seconds). The concrete relay attachment still needs publication-pressure and current-head snapshot qualification.

Recovered generations now retain their directory inode anchor through an admitted retirement handle and small aliasing control. Final release from a pinned source or copied generation queues directory FD closure on the cleanup worker, matching mapping/image/lease retirement. The public generation API is unchanged. Recovery consumes one additional retirement resident; custom domains sized exactly for a mapping and view must permit three residents instead of two. Reader-only linkage remains unchanged.

Eight native reader/pinned-source/coordinator checks pass in 2.55 seconds; the corresponding ASan/UBSan set passes in 3.29 seconds and TSan in 16.54 seconds. The pinned-source fixture explicitly checks all three residents while live and zero after drain, including retained copies, saturation and generation lease release.

### Durable bundle qualification

The optional `detent::relay` core connects a configured `Bundle` to the coordinator's publication sink. A staged object becomes deliverable only when its candidate descriptor, directory identity and target match the actual recovered generation. Fresh upstream authentication gates readiness. Cancellation releases input borrows before coordinator disposal; fencing immediately revokes offers. A refused cache allocation leaves publication capacity available and produces a continuity gap. A pinned current-head image can then rebuild the forced snapshot incrementally.

The TLS coordinator fixture independently decodes both raw and zstd cache frames after upstream durable feedback. Native cases cover empty state, a saturated 64 MiB cache child, 100,000 knobs and 500,000 knobs. All five checks, including certificate setup, pass in 14.64 seconds. Empty-state and cache-pressure cases also pass under ASan/UBSan (1.51 seconds) and TSan (5.98 seconds). The resource hierarchy reserves a 256 MiB publication child and 64 MiB cache child beneath a 384 MiB root; runtime/control allocations in this fixture belong to the publication child.

| Knobs | Publication heap peak | Cache heap peak | Combined heap peak | Mapped peak | Disk peak | Fixture wall time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100,000 | 19,505,868 B | 6,563,904 B | 25,336,908 B | 8,204,288 B | 8,200,587 B | 2.423 s |
| 500,000 | 97,165,324 B | 29,598,720 B | 126,031,180 B | 41,000,960 B | 41,000,587 B | 10.853 s |

These Intel macOS measurements include receive, validation, dual encoding, complete-image publication, independent fixture decoding and shutdown. They measure admitted capacities, not process RSS or TLS/framework allocations. Child peaks occur independently; their sum need not equal the simultaneous root peak. The maximum observed 1 ms timer lateness is 583 µs at 100,000 knobs and 4,144 µs at 500,000 knobs. Timing ends before the fixture's synchronous independent decoding. Logs are retained in `.scratch/relay-coordinator/footprint-tests.log` and the native CTest log.

This qualifies durable bundle integration, not Stage 5B as a whole. Downstream HTTP hosting, authenticated exact-bundle routing, shared-session settlement, restart/failure tests, 500 same-bundle subscribers and native Linux/FreeBSD qualification remain required before the consolidated Architect review.

`SubscriptionRequests` now lives in `detent::distribution`, with authority aliases preserving source compatibility. The same bounded Open, Next and Close admission lanes can serve both hosts without importing authority state or consensus. A retained lease keeps the body and its quota occupied until accepted transport originals release it. The existing admission and subscription fixtures pass natively (0.61 seconds), under ASan/UBSan (0.79 seconds) and TSan (1.38 seconds), including independent Close capacity, per-principal refusal, allocation failure and concurrent lease release. Consumers rebuild for the namespace change; this unreleased internal authority type has no stable binary ABI.
