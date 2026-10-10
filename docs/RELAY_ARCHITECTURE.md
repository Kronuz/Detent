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
