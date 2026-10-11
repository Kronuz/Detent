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

The optional `detent::distribution_http` module shares owned reply pumping, transport settlement callbacks and binary update headers. Authority delivery uses it for control replies and pending frames. A successful submission releases only the producer's reference; the transport retains its original buffer and request lease through completion. Cancellation does not report a written reply or release those originals early. Session feedback and source retirement remain host responsibilities.

The pump fixture holds header and body writes, exercises backpressure, rejects incorrect/duplicate completion serials, cancels an accepted original and checks final physical release. Native pump plus managed subscription process checks pass in 6.38 seconds. Authority distribution, ownership and held-header checks pass natively in 4.16 seconds. The pump and those three authority checks, including certificate setup, pass under ASan/UBSan (4.84 seconds) and TSan (5.41 seconds). This extraction preserves protocol headers and source behavior; it does not establish fleet scaling.

### Validated host blueprint addendum

The Lead Software Architect's max-effort pass approves the remaining Stage 5B component boundary. One transport-free `SessionTable<Marker, Binding>` owns bounded slots/indexes, principal caps, incarnation/request counters, expiry, grant fingerprints and publication windows. Authority retains its authorized-scope binding and history handoff; relay uses a finite configured bundle index and cache marker. Adapters supply current cursor and continuity. The authority wrapper preserves its signatures and maps transient common source lag to its existing out-of-sync behavior. Collection evaluates a bounded, nonthrowing retirement-capacity callback once and retains that debt until release.

Shared HTTP parsing enforces exact fields, bounded nesting/counts, complete cursor identities, profile and coding. Parsed commands retain admitted input through processing. Authentication supplies immutable principal/grant views; every request reauthorizes its original binding. Relay Open cannot create a bundle or rewrite its principal, policy epoch, Context or canonical namespaces.

The relay scheduler shares cached objects across sessions and retains an offered object through authentic transfer settlement. Its separate poll, control, bootstrap-transfer and catch-up capacities use bounded round-robin work and deadlines. Source lag returns HTTP 503 without closing the session; cache continuity loss requires a snapshot even when accepted base equals head. Snapshot allocation pressure requires retry backoff. Fencing stops new submissions while accepted originals drain.

The optional relay executable uses existing TLS/listener and completion facilities with fixed workers. Configuration validates finite bundle/domain capacity, duplicate durable roots, exact scope identities and a complete heap partition. Readiness requires recovery plus fresh upstream authentication for each required bundle. Shutdown drains listener originals, coordinators, storage and retirement domains before workers stop.

| Architect check | Required implementation guard |
| --- | --- |
| Scalability | Fixed admitted capacity and shared payload independent of subscriber count; measured concurrent fan-out and control progress under saturation |
| Edge cases | Explicit outcomes for early/conflicting feedback, stale tokens, lag, gaps, expiry, Close during transfer and fencing |
| Dependency and state | Distribution-only session core, optional HTTP, preserved authority APIs, retained bundle/credential/domain ownership |
| Security and scale | Exact principal/bundle isolation, request reauthorization, TLS chain/hostname validation and bounded preauthentication intake |

Acceptance requires authority equivalence fixtures, held real transport originals, TLS authority → relay → agents → separate readers, multiple isolated bundles, restart/revocation, 500 concurrent same-bundle subscribers, 100,000/500,000-record publication and footprint/CPU/egress evidence. Native Linux/FreeBSD, sanitizer and relay-only/agent-only/reader-only/legacy builds precede one consolidated component review. This is design approval; Stage 5B hosting is not yet accepted. Stage 5C immutable publication and Stage 6 sustained fleet qualification remain separate.

`SessionTable<Marker, Binding>` implements that common lifecycle without authority state or HTTP linkage. `Subscriptions` remains an authority wrapper with scope/account validation and history continuity. Common source lag preserves the session; the wrapper maps it to its prior out-of-sync result and closes a regressed publication. Compile-time constraints require nonthrowing marker copies/moves and binding retirement moves.

The shared-core fixture exercises 500 independent windows, early feedback before settlement, policy changes, transient source lag, equal-head continuity loss, Close during transfer, slot reuse with a new incarnation, stale tokens and final account release. These are table checks, not evidence of 500 concurrent network subscribers. Native core plus publisher-pressure, 100,000-knob idle, large-scope and authority session checks pass in 24.89 seconds. Authority distribution, ownership, held-header and session checks with the shared core and certificate setup pass under ASan/UBSan (4.92 seconds) and TSan (6.00 seconds). Authority source signatures remain compatible; consumers rebuild for moved internal metadata/types.

The common strict request parser admits JSON values, strings, parse-stack growth and namespace directories into one physical page arena. Open namespaces normalize to a sorted unique set, preserving authority scope behavior. Next and Close enforce exact fields/version, complete lowercase nonzero identities, unsigned cursors/window feedback and bounded poll waits. Input size, nesting depth and namespace count are bounded before a command reaches its host adapter.

Retained parser commands use retirement handles: final owner release queues cleanup and returns one arena page per worker turn. The intake-worker variant performs parsing and bounded arena destruction on that worker, for hosts whose request callbacks already run off the owner executor. Neither variant exposes RapidJSON in its public API. Parsed views own their backing arena after the original input changes; the retained variant also keeps its sealed body alive until retirement.

The parser fixture covers canonical namespace normalization, 4,096 namespaces, raw/zstd, exact Next feedback, Close, duplicate/unknown fields, incomplete/null feedback, invalid versions/identities/integers/waits, nesting/NUL bounds, byte-limit refusal, allocation pressure, input retention and final physical release. It passes natively (0.30 seconds), under ASan/UBSan (0.34 seconds) and TSan (0.62 seconds). The 4,096-namespace case admits 403,481 bytes of parser arena capacity on the qualification host. Authority/relay routing integration remains part of the host component.

Authority distribution and session endpoints now use that shared parser on their existing HTTP intake workers. Open is parsed once; immutable canonical namespace views feed account-backed scope construction and binary-search membership in the credential's admitted grant view. Existing `AuthorizedScope::authorize` callers remain compatible through normalization; the canonical entry point rejects unsorted/duplicate requested names, unsorted grants and unauthorized subsets. Queued tasks retain their existing body lease and independently stored scope, so no parser view escapes worker cleanup.

The legacy distribution request parser now has a 1 MiB ceiling even when the authority application's transaction-body limit is larger. Default 256 KiB requests keep their configured bound; publication-command limits are unchanged. This bounds distribution grammar independently of ingestion sizing. Existing protocol clients default to 256 KiB request capacity.

Scope, real TLS distribution, retained-original, held-header, large-scope and managed subscription process checks pass natively (eight checks, 13.00 seconds). Scope and the four authority delivery/ownership cases with certificate setup pass under ASan/UBSan (six checks, 7.77 seconds) and TSan (six checks, 10.24 seconds). The canonical scope fixture also rejects reordered names, duplicate names, unordered grants and insufficient grants. No throughput improvement is claimed for this extraction.

`detent::relay::Sessions` adapts the common table to finite bundle indices and cache markers. The trusted intake resolves exact namespace/profile bindings and supplies an immutable authorization mask derived from current credential grants. The adapter independently checks effective principal, policy epoch, configured identity and readiness. Each Next and Close reauthorizes the same binding. Fencing refuses new Next requests while allowing an authenticated holder to close; prepared originals retain their independent frame ownership through settlement.

The TLS coordinator fixture retains 500 simultaneous backend offers from one durable bundle, with alternating raw/zstd session selections. Every offer points to the same encoded object; staging counts stay unchanged and cache capacity does not grow. Cases reject unauthorized masks, changed principals/policy epochs and ahead bases, preserve early-feedback settlement ordering, fence pending offers and drain Close/expiry metadata only after settlement.

Native empty-state, 100,000-knob and 500,000-knob cases with certificate setup pass in 13.55 seconds. The adapter/offer metadata child peaks at 376,832 bytes in all three cases. The 500,000-knob case retains the existing 29,598,720-byte cache peak and 126,031,180-byte combined peak; publication peak occurs before fan-out metadata allocation. Empty-state adapter checks with certificate setup pass under ASan/UBSan (1.19 seconds) and TSan (3.34 seconds). These are 500 simultaneous backend offers over a real upstream TLS publication, not the required 500 concurrent downstream network transfers. Listener hosting and that fleet gate remain ahead.

### Shared private source configuration

`detent::delivery_config` owns source configuration loading for managed delivery hosts. The agent retains its private header aliases and existing configuration syntax. The result includes the effective principal, policy epoch and whether the delivery mode was explicitly configured, so relay startup can validate exact bundle identity and require subscriptions without changing agent defaults. The shared startup-worker file reader preserves the single-link regular-file, owner, permission, size and complete-read checks; credentials remain outside public reader configuration.

The native credential/configuration fixture and both TLS agent process tests pass with certificate preparation in 10.99 seconds. The credential/configuration fixture also passes under ASan/UBSan and TSan. Logs are retained in `.scratch/relay-config-{native,asan,tsan}-tests.log`. This extraction introduces no performance claim; relay daemon hosting remains under implementation.

### Relay startup configuration

`detent::relay_config` validates a private JSON host configuration on the startup worker. It requires server TLS material, one to 256 source/durable-directory bindings and one to 10,000 downstream credential entries, within the private file reader’s 1 MiB limit. Source files retain the managed source grammar. An unspecified source delivery mode becomes subscription; an explicit poll mode is rejected. The default listener is loopback port 8318. Each credential grants explicit bundle indices with matching principal and policy epoch; startup stores token hashes and grant fingerprints rather than bearer tokens.

Startup rejects duplicate canonical durable paths, duplicate configured identities, duplicate credential tokens, invalid or duplicate grants, unknown fields and an undersized root heap budget. The root covers baseline, control and every bundle’s publication/cache ceiling. The host must additionally verify actual opened directory identities before becoming ready; canonical path checks alone do not establish physical directory identity. Source/configuration parsing uses bounded startup-worker storage; the live registry must copy retained metadata into its admitted baseline account.

The configuration fixture checks TLS, default listener, subscription selection, exact principal/policy grants, duplicate identities/paths, body grammar and budget refusal. It passes with certificate preparation natively (1.05 seconds), under ASan/UBSan (1.47 seconds) and TSan (1.96 seconds). Logs are retained in `.scratch/relay-host-config-{native,asan,tsan}-tests.log`. The daemon listener and service integration remain under implementation.

### Immutable intake registry

`detent::relay::Registry` copies finite bundle identities, canonical namespace names and credential metadata into admitted storage during startup. Authentication hashes a bounded bearer token and searches the immutable sorted digest table. Open routing requires an authorized bundle bit, the exact configured cursor identity, matching principal/policy and the complete namespace sequence. Empty namespace scopes remain distinct. No intake request creates or rewrites a bundle. The host retains the registry until intake and accepted originals drain, then destroys its pages off the owner loop.

The fixture checks independently owned metadata, empty scopes, foreign names, revoked masks, policy rotation, duplicate identities/tokens, out-of-range grants, allocation refusal and final zero usage. Eight threads perform concurrent authentication and routing against one immutable registry. Configuration and registry fixtures pass with certificate preparation natively (1.00 second), under ASan/UBSan (1.27 seconds) and TSan (1.84 seconds). Logs are retained in `.scratch/relay-registry-{native,asan,tsan}-tests.log`; these are correctness checks, not a throughput benchmark.
