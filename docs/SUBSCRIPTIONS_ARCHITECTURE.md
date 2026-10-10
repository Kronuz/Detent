# Authenticated subscriptions and publication windows

**Date:** 2026-10-09
**Status:** Validated Architectural Blueprint from the Lead Software Architect, max reasoning effort. Stage 5A implementation is in progress. Snapshot/history handoff, bounded owner session state and strict export selection are qualified. HTTP session transport, agent publication feedback and assembled acceptance remain open.

## Purpose

An unchanged agent waits with bounded session metadata instead of repeatedly capturing and encoding authority state. Each session permits one publication window. The agent advances that window only after its exact offered cursor is durably selected and recovered.

The existing `/v2/distribution` endpoint and `receive(ClientOptions)` remain available. Subscription responses reuse profile-1 framed updates and the existing immutable local image format. Relay fan-out and incremental local publication are separate Stage 5B and 5C components.

## Protocol

| Endpoint | Request | Success |
| --- | --- | --- |
| `POST /v2/distribution/session/open` | Version 1, authority identity, canonical scope, namespaces, profile, coding, optional actual durable base and `force_snapshot` | Bounded JSON with session ID, snapshot/resume mode, initial window 0 and negotiated limits |
| `POST /v2/distribution/session/next` | Version 1, session ID, `after` window, optional exact `published` cursor and bounded `wait_ms` | One framed update, or 204 when the poll produces no update |
| `POST /v2/distribution/session/close` | Version 1 and session ID | Idempotent 204 |

Every request authenticates. A session binds the effective principal, policy epoch, canonical scope, authority identity and profile. A rotated credential must preserve that binding and still authorize every stored namespace. Possession of a session ID grants no access.

Update headers bind the response to its negotiated session version, ID and offered window before any candidate is accepted. A 204 response advances no cursor and makes no linearizable freshness assertion. Bounded control errors distinguish `snapshot_required` (409), `identity_changed` (409), `session_out_of_sync` (409) and `session_expired` (410); simultaneous polls receive a bounded busy response.

An explicit `snapshot_required` closes the session and requires reopening with `force_snapshot:true`. Session loss alone allows fresh quorum negotiation from actual durable selection. Identity changes still require trusted configuration authorization; a transport control cannot grant an epoch or scope transition.

## Owner state and continuity

```text
Open → Idle → Parked → Quorum → Exporting → AwaitPublication
         ↑                                  |
         └──── exact published feedback ────┘
Any state → Closing → originals and retirement drain → Free
```

Open passes through the quorum-read path without retaining a snapshot. It rejects a same-identity future base. Resume mode saves the exact history handoff at the requested base; absent or unavailable history selects snapshot mode. The snapshot target is frozen only when Next acquires the existing export slot.

Before exposing bytes, the owner stores the offered target and the export's handoff in the session. That marker survives export retirement and becomes the base marker after publication feedback. The [handoff capability](AUTHORITY_EXPORT.md#snapshothistory-handoff) detects eviction, owner replacement and same-context resets without retaining history bodies or authority roots. It promises detectable continuity rather than reserved history; the current frozen update may finish after subsequent coverage disappears.

The session stores only the last accepted window/cursor and one outstanding offered window/cursor/marker. Exact duplicate feedback for the accepted window is idempotent. Skipped windows, mismatched cursors and missing feedback for an outstanding offer fail explicitly. Feedback arriving before original stream settlement may occupy one pending request; it cannot release the preceding resources or capture another root.

Export selection is explicit: legacy callers use `Automatic`; sessions use `SnapshotOnly` or `ResumeOnly`. Resume-only export produces delta/progress or a typed reset before headers. It checks the saved base handoff before capture and at completion of delta gathering. Once gathering owns the complete immutable input, ordinary eviction need not interrupt that update. History loss and representation limits are distinct from allocation pressure and internal errors.

## Admission and scheduling

Subscription requests use separate admission from publishers, including when many agents share a read-only credential. Configurable bounds cover session slots, parked polls, control requests, per-principal occupancy, bodies and replies. Next/Close have small fixed body bounds; Open has a scope-sized bound. Control requests and publisher connections retain headroom when the long-poll allowance is full.

The delivery ledger physically admits session bookkeeping, scopes, request controls, owned replies and retained originals. Existing authority-root accounting remains separate. A session contains no private transaction-history queue or frozen authority root.

Parked polls remain outside the quorum and capture queues. Applied revision advancement makes them eligible for bounded fair promotion through the existing quorum path. The owner rechecks continuity after that boundary. Idle polls do not repeatedly submit quorum reads or construct exporters. Lookup avoids unbounded attacker-controlled hashing; expiration and wakeups use bounded round-robin work.

Sessions belong to one process and use monotonic deadlines for idle lifetime, maximum lifetime, polling and transfer. Expiration or Close revokes new work immediately, while accepted reads, originals and retirement keep the slot occupied until drainage. Request callbacks retain independently admitted state; tasks identify sessions without owning the table that contains them.

## Agent integration and acceptance

The additive client API opens a session, receives offer/idle/reset events and closes it. It reuses incremental TLS reception and budgeted decoder disposal while preserving the legacy receiver contract. An explicit coordinator delivery mode preserves existing configuration behavior.

The coordinator opens from its recovered actual selection, accepts one offer, completes existing construction/publication/recovery, then piggybacks exact publication feedback on Next. Cancellation or ambiguous transport/publication outcomes invalidate the session. Originals drain and actual selection is reconciled before reopening; selected state does not imply acknowledged publication.

Consolidated acceptance covers concurrent writes during slow snapshots, history loss during gathering and before feedback, authority restart, expiry during publication, missing/duplicate feedback, concurrent requests, credential/scope isolation, cancellation after selection, held-original shutdown and table/account saturation. Idle 100,000/500,000-record fixtures must show no repeated exports or frozen roots. Saturated long polls must preserve ordinary publisher admission and quorum progress. Managed metadata, retained controls and owner work are measured separately from TLS/framework baseline. Component review follows complete implementation and qualification.

## Implemented foundation and evidence

`Subscriptions` admits its fixed slots, ordered indexes and retained authorization scopes through the delivery ledger. It retains no authority roots. Request and offer tokens include slot incarnations, so late settlement from a previous occupant cannot release a reopened session. Close and expiration preserve occupied slots until accepted requests and transfers settle; scope retirement consumes bounded byte work.

`DeliveryExport` supports strict snapshot and resume selection. Resume checks the original base handoff and reports typed history or representation resets before headers. Storage pressure remains an admission failure. The legacy automatic export path preserves snapshot fallback.

The owner fixture covers exact publication feedback, duplicate feedback without ledger growth, feedback before transport settlement, same-context history reset, stale tokens, principal and policy isolation, future bases, table saturation and retained-transfer expiration. A real transaction fixture advances three publication windows, including a transaction outside the selected namespace. The export fixture freezes revision R while R+1 commits and verifies the snapshot contains R's value.

The delivery-export and session fixtures pass native macOS, ASan/UBSan, TSan, Linux and FreeBSD. This qualifies the foundation, not the proposed HTTP endpoints or fleet behavior. Transport settlement, parked-poll scheduling, agent feedback and the large-state idle and publisher-pressure fixtures remain required for Stage 5A acceptance.

`SubscriptionRequests` provides thread-safe admission independently of publisher counters. Open, Next and Close each have a bounded lane, with separate principal quotas for polls and controls. Saturating polls or opens leaves Close capacity available. Leases allocate body backing through the common delivery ledger before accepting bytes and preserve their slot while an original retains the lease. Closing the pool revokes new requests without releasing outstanding storage. The eight-thread fixture completes 8,000 acquire/release cycles and checks that all body and index charges return to the idle baseline; allocation failure preserves prior admissions. The fixture passes macOS, ASan/UBSan, TSan, Linux and FreeBSD. HTTP binding of these lanes remains pending.
