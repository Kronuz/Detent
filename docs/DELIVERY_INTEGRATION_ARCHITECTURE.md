# Validated Architectural Blueprint: authenticated authority-to-local-view delivery

**Status:** Lead Software Architect design GO (frontier model, max effort), October 8, 2026. Implement the complete component autonomously, qualify it, then obtain one consolidated source review. The corrected [DiskStore component](DISK_STORE_INTEGRATION.md) has source GO; its final qualification remains a prerequisite. This component completes the production integration within Stage 4. Durable resumable sessions, relays and incremental page publication remain Stage 5 work.

```text
Authorized scope → Frozen authority revision → Bounded encoded frames
                                              ↓
Exact pinned base → Validate/apply → Build image → DiskStore selection
                                                       ↓
                                            SDK worker refresh → ReadView
```

The boundary includes exact-base application, canonical authorized scope, authority snapshot/delta/progress export, bounded transport ownership, the actual agent pipeline and SDK pinned refresh. Do not offload `Worker::run_one`. Authority traversal and deferred-value retirement stay on the existing owner executor; independently owned encoded buffers can cross to transport.

## Exact-base application

Add a private pinned-image source exposing validated namespace, record and override iteration without constructing a legacy `Snapshot`. Retain its mapping, shared generation lease and complete descriptor. Extend `ImageBuilder` with validated delta/progress input while preserving the existing snapshot constructor.

Require complete base Cursor equality, compatible profile and exact pinned-generation evidence. Recheck the actual selected descriptor before publication; an acknowledged cursor is insufficient after cancelled selection. Preserve untouched key revisions and assign changed keys their actual transaction revision. Advance touched namespace watermarks to the corresponding transaction revision.

Build a charged mutation index and sort it with resumable bottom-up merge passes by namespace/key and transaction order. Collapse repeated keys to their final mutation, then merge once against the ordered base. The target is approximately `O(M log M + N)` work plus text and override bytes, rather than a complete rebuild per transaction. Count the final layout before allocating the output buffer. Pin the base and validated delta throughout construction, then discard indexes/pages under work budgets.

Progress preserves all namespace/key state and builds a complete image with the new global Cursor. Neither progress nor a delta can advance durable publication metadata without a matching selected image. Same-Cursor delivery must compare canonical state identity; differing state is a conflict.

## Canonical authorization and export capture

Add authenticated `/v2/distribution` with bounded namespace selection, optional complete base Cursor, supported profile and explicit raw/zstd negotiation. Reuse the quorum-read boundary before capturing immutable `State` roots on the authority executor. Never export private checkpoints or invoke eager `State::snapshot()` conversion.

Introduce bounded `AuthorizedScope` and authority-only `ExportCapture` values. Validate and authorize each name before capture; an empty requested set means an empty scope. Hash a versioned, length-framed representation of stable configured authorization identity, authorization-policy epoch and canonical namespace names. Credential secrets do not enter the scope identity, and token rotation alone need not change it.

Add bounded persistent-map cursor access for count/emission passes over the same immutable capture. Whole-tree `for_each` cannot run in a service turn. Incremental snapshot/delta/progress encoders preserve the established tuple schema and canonical integer forms, with independent vectors checking interoperability.

## Applied-change coverage

Retain a finite in-memory cache of actually applied changes with transaction and physical-byte limits. Record only changed keys; an `Applied` retry receipt is not another transaction. Stage entries until the enclosing authoritative state publication succeeds.

Maintain an exact coverage frontier. Eviction gaps, capture failure, restart and snapshot activation invalidate affected coverage; unavailable bases receive snapshots. Progress is allowed only when complete coverage proves that no scoped change occurred. Cache pressure must not fail a committed transaction: stop recording the optional suffix, mark the gap and continue authoritative application.

Charge cache pages, export pins and retirement together. A slow response cannot keep history alive after its admission charge disappears.

## Owned transport output and reception

The current HTTP writer appends to a complete body, and `DeferredReply` owns a complete response. Add an optional owned streaming-response capability to the generic HTTP layer while preserving buffered APIs and default unsupported behavior.

Use one pending chunk slot and one accepted-write obligation. Offering a chunk transfers immutable ownership. The next chunk becomes writable only after authentic settlement. Close rejects new chunks and cancels/reaps accepted network work before releasing retention. Bound headers and deadlines; do not concatenate the response or detach a callback ownership graph.

Preadmit sender frame buffers and static compression workspace through actual allocation accounting. Agent reception is incremental and reuses certificate/hostname verification without using the whole-body HTTPS health helper. Enforce status/media-type parsing, duplicate-header rules, exact framing completion and compression negotiation.

## Production agent

Add managed delivery with explicit authority identity, credentials, canonical scope, local directory and heap/mapping/disk limits. Retain legacy JSON as a named compatibility mode with its existing version contract. Managed delivery replaces `Client::on_update → ShmWriter` with an admitted coordinator owning one candidate:

```text
Recover → Receive → Validate → Apply/count → Build → Publish → Reconcile
              └──────────── failure/discard ────────────┘
```

Pause intake while the candidate occupies capacity. Track received, validated, actual selected, acknowledged and recoverable cursors separately. Reconcile cancelled successful selection from `DiskStore::selection()` before making another base-dependent request.

Context, scope and restore-epoch changes require an explicit trusted controller transition. Add a narrowly typed reset-publication path binding the expected current descriptor to the configured new identity. Candidate bytes cannot authorize reset, and the directory storage identity remains unchanged.

Startup and full selected-image recovery run on the startup worker. Runtime disk requests use the retained completion driver. Shutdown drains candidate disposal, accepted transport originals and DiskStore operations, then the application-owned retirement domains.

## SDK and dependency boundary

Add worker-side `GenerationReader::refresh()` and atomic installation of fully validated pinned views. `read_view()` acquires the installed root without filesystem or networking work. Failed refresh preserves the previous usable root; retirement workers release mappings and leases.

Keep live state separate from baked defaults and CLI overrides. A scoped SDK view pins its live root and immutable evaluation overlays. Deletion falls back to defaults only on absence; type/conversion errors retain existing behavior.

Preserve `Client` constructors and legacy methods. Introduce an additive local-client adapter and scoped `Client::read_view()` support without eager compatibility snapshot copies. Factor the small shared semantic layer, including types, validation, hashing and errors, where necessary to prevent a reverse dependency cycle. The reader target requires no networking, distribution codec, completion or consensus dependency.

## Implementation and acceptance

1. Implement pinned source iteration and exact-base construction.
2. Implement canonical scope and incremental encoders.
3. Capture applied changes and expose authenticated export.
4. Add owned transport streaming and incremental reception.
5. Integrate the actual agent and SDK paths.
6. Qualify the assembled component and obtain one consolidated review.

| Area | Required evidence |
| --- | --- |
| Semantics | Independent snapshot/delta equivalence; repeated keys, deletes/defaults, untouched revisions, empty scope and irrelevant progress. |
| Identity | Exact-base rejection, scope changes, trusted epoch reset, same-Cursor conflict and authenticated namespace isolation. |
| Authority | Frozen export during concurrent writes; cache eviction/restart gaps force snapshots; producer-private state never escapes. |
| Ownership | Allocation refusal, held network/storage originals, facade loss, weak controls, slow consumers and shutdown retain charges. |
| Processes | Authenticated authority → agent → separate SDK readers; process kills, restart and pinned-reader GC pressure. |
| Scheduling | Large exports and candidate construction let timers, consensus and cleanup progress under shared budgets. |
| Capacity | Representative 100,000/500,000-record snapshots and adversarial many-transaction deltas; actual peak heap, mappings, disk debt, CPU and latency. |
| Compatibility | Legacy JSON/Client, journal/Xapiand, agent-only and reader-only builds, three platforms and relevant sanitizers. |

The scalability check bounds retained candidates, frames and history and merges each base once. Edge-case analysis requires complete cursor/descriptor binding and trusted reset authority. The dependency/state audit separates authority roots, encoded transport ownership and SDK reader targets. Security review binds export to canonical authorized scope and requires authentic IO settlement before retention release.

These gates can establish Stage 4 behavior and measured limits. They do not establish Stage 5 resume guarantees or fleet throughput.
