# dense-distribution-build: journal

State: DD3 step 1 (pages frame) done at Detent:0e63b069; steps 2-4 next (lead msg: start DD3 now, order 1-4). Waiting on job 261120-092532-58e85e (relay system tests 5x -j4, D136) and CI. Dev build in .scratch/build (Release, suite flags), current at Detent:0e63b069.
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Step 7 (former DD3, then DD4's wiring): the plan is under Next 4.

## Done

- Steps 2-4: `distribution::FrameInput` (test_frame_input); DiskStore cycles of named plans,
  selection and recovery per table entry (D8); the coordinator on one W5 session with per-namespace
  records; readers attach by name (C3) (Detent:6906fb4a).
- Step 5: managed.json `namespaces` or all, `retry_interval_ms`; v1 client, probe and
  PublicationSink removed; host readiness per namespace (C4) (Detent:db87768a).
- Merged dd4's final branch (Detent:5f21ccba) and the relay helper's branch (Detent:26ab90b4).
- Tests: test_namespace_coordinator (cold start, entries, blocks, W7, repair, drop, offline and
  resumed restart) replaces the v1 coordinator tests; managed process test on v2 (Detent:26ab90b4).
- Step 6: system harness on per-namespace positions; the authority leader enables positions as it
  leads; GenerationUnavailable is fatal; a pooled malformed close fails closed (Detent:ac278e50).
  test_delivery_worker_lifetime holds the first full copy's build on the worker (Detent:514349e0).
  Local runs: 67/68 system and authority (relay_ipv6 then passed alone), worker and coordinator.
- Managed docs on W5 sessions, GCC `<vector>` include (Detent:1fadf5c5).
- Merged namespace-local-cursor at Detent:296228c3 (Detent:e52e8d31): the coordinator's v1 bridge
  goes, FrameInput sets a full copy's W1 target, single-plan `begin` publishes `disk_store_entry`
  (Detent:b8c2cf69). The v1 export seam (`detent::local_export`) and the agent's v1
  --generation-* identity flags go (Detent:410242d4). Local suite after the merge: 201/206, the
  five failures fixed and passing on rerun (startup and foreign-generation tests now expect C2's
  failed entry, not a fence; writers.py dropped a field the fakes lack).
- managed.json examples in the quickstart and guide carry v2 fields only; docs e2e passes
  locally against the build (23 blocks, 0 failed) (Detent:501f9c4d).
- Authority enables positions once its history is ready; the distribution service test runs W5
  first (its v1 sequence raced the enabling entry: 5/5 pass after) (Detent:4b0736e5). Relay
  mirror `reconnect_maximum_ms` and sync-failure logging (Detent:d9668727).
- Suite 261120-085935-8d23e7: 205/205 at Detent:d9668727.
- D136 idle seal: `AdministrativeKind::Seal`; the leader proposes it when positioned changes stay
  unsealed for `ServiceOptions::seal_after` (1 s); test_history_layout's seal_entry case
  (Detent:67a16fab). Design doc "Seal" rule 3, RELAY.md staleness line.
- D136 damaged entry: a selected entry's recovery failure (not pressure) calls `DiskStore::fail`
  and requests a repair; test_namespace_coordinator damages every `.owner` and both entries are
  replaced (fails without the fix: local mutant run timed out on the case) (Detent:be42ee82).
- Suite 261120-092520-b041d1: green at Detent:b6e0bea7.
- DD3 step 1: W6 `FrameKind::Pages` = 5, header [name, 5, after, cursor, root, chunk count], then
  bin chunks of 64 KiB (the last 1..64 KiB); the decoder raises the tokenizer's bin bound
  (`binary_limit`) to a chunk inside the frame only and bounds the transfer by `limits.bytes`;
  `PagesAnswer` on NamespaceRequest makes the encoder send one; FrameInput holds root and bytes
  (`root()`, `transfer()`), FramePreparer::Prepared and StagedFrames forward them.
  test_session_frames' pages_frames case (round trip at 3 feed sizes, zero root, short inner
  chunk, arity, body bound) (Detent:0e63b069).
- Agent reads credentials through `detent::internal::read_private_configuration`; README drops
  local_export, bundles and flat-v1; tls-fixture-diagnostics' coordinator mode repeats
  test_namespace_coordinator (Detent:407aed82). Docs link fix for the CI Docs failure at
  Detent:501f9c4d (Detent:c96dc63b).

## Next

1. Port to the W5 fixture what the retired v1 tests covered: hash-once (D12.2), scrub damage to
   repair, prepared startup; a W5 fleet load for the measurement arms.
2. (done, D136) Damaged entries.
3. `distribution::UpdateCache` is unused; ACTORS.md and RELAY_ARCHITECTURE.md describe bundles.
4. Step 7 (former DD3). Lead order: 1 pages frame (done), 2 relay generation and canonical copy,
   3 agent transfer -> plan with no packer through DD1, 4 fallback to records. Done means a 100K
   cold agent boots from pages with no packer, as a test job; then suite + CI, "ready for review".
   Open wiring decisions (mine to make, record under Rulings): the receiver opts in with
   `"pages":true` on W5 open (relays downstream and old agents get records); a next names
   `"records":[names]` to fall back after a refusal (D9: one other source, then records). The
   relay's RingSink (namespace_relay.cc:148) must refuse a pages frame (a relay mirrors records).
   Map:
   a. Local: a transfer plan builder (friend of PagedPlanState/PagedImage): name by
      `next_generation`, `derive_segment` as TransferSegmentId, `receive_transfer`, fill a
      PagedImage (Root needs records and namespaces counts the receiver does not return), then
      reuse PagedSnapshotPlanBuilder's CandidatePagedValidator tail for counts and histogram.
   b. Wire: W6 `FrameKind::Pages = 5` with encoding and 32-byte root commitment; the decoder's
      tokenizer caps bin at 32 bytes and buffers payloads whole (session_frames.cc:487), so pages
      go as one bin token per page (<= 64 KiB) with a raised per-token bound. FrameInput gains the
      transfer bytes.
   c. Sender: the relay (build tier, D7) holds no generation: it builds one per namespace from its
      mirror (full build at cold start, D7 after), keeps the canonical copy (DD2) and PageWindow,
      and answers a cold or below-cut session with Pages; the authority keeps records (Limit 6).
   d. Agent: build_frame takes Pages -> plan (a); a refusal asks records (one other source, then
      records, D9); delta pages through `receive_delta`.
   e. Tests: no packer is `hash.built.page_calls == 0 && pack_calls == 0` (CoordinatorStatus.hash)
      on a 100K cold agent; root mismatch falls back to records; a resumed fetch.

## Messages

- msg-1 (mine, to the folded namespace-local-cursor writer): its points stand as this branch's
  plan; the interim bridge (`namespace_cursor`, `distribution::frame_cursor`) is in
  Detent:6906fb4a, and the W1 local change deletes it when it merges here.

## Coordination with namespace-local-cursor

That branch moves the local types to the W1 cursor (Descriptor, paged::Root, builders take the W1
target from the caller). This branch's coordinator speaks W1 natively, so the coordinator's
v1<->W1 boundary mapping that branch plans (its journal, "Coordinator changes") is not needed:
builders taking a W1 target is all this branch needs from it. DiskStore's multi-entry change
(step 4) is this branch's; the single `scope` entry there is replaced here.

## Blocked on

Nothing. (Resolved by D136, kept for the record:) Lead ruling, relays and sealed history (the cause of test_system_managed_relay[_ipv6] failing
under load: 2 of 5 under `-j4`, 0 of 3 alone). The leader now enables positions as it leads, so
seal 0 is cut before the harness seeds; seals fire only every 2048 transactions (or 1/8 of the
log), and a relay mirrors sealed history only, so a quiet cluster's later writes never reach a
relay. Measured: in the failed run every voter cut one seal (seal 0, before `seeded`), and the
relay mirror stayed empty for 30 s with no sync failure. Before, a relay's first layouts
request triggered enabling, after the seed, which hid the gap. Options:
1. The leader proposes a forced seal when positioned changes stay unsealed for an interval
   (recommended: bounded relay staleness at any write rate; deterministic, as a log entry).
2. Relays also mirror the unsealed tail from W5 frames (larger; relay design change).
3. Enable on demand again (a W5 next or layouts request), which hides the gap for tests only.

The workflow and README items are fixed (Detent:407aed82); ci.yml keeps namespace-local-cursor's
removal of test_local_view, which no longer exists.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-085935-8d23e7: 205/205 at Detent:d9668727; 261120-081334-e4f6a8: 205/205 at Detent:501f9c4d (relay system tests flaky under load: see Blocked on; 261120-080129-538d87 failed them at Detent:410242d4) |
| A2 | <no evidence yet> |

## Result

<none yet>

## Rulings I made

- Poll mode, `delivery_mode`, and the v1 identity fields (`group`, `epoch`, `principal`,
  `policy_epoch`, `reset_from`) leave managed.json: W5 sessions carry cursors, and a new
  incarnation is a full copy (W7), so no configured identity transition remains (greenfield).
- The relay's PublicationSink goes with its bundles: it had no other consumer.
- The v1-only tests (test_coordinator, test_repair, test_hash_once, test_prepared_startup,
  test_preparation_deadline, test_client, test_response, test_probe_*) and the v1 fleet load
  (benchmarks/fleet_load, test_system_fleet_*) are removed with the v1 client; their W5 ports
  are Next 1.
- `delivery::read_private_configuration` goes; the agent uses the shared reader (its messages
  now say "private configuration").
- D136's interval is 1 s (`ServiceOptions::seal_after`, no config key): the relay tests wait 30 s,
  relays sync every 250 ms, and no doc promises a tighter bound. A write rate below 2048/s now
  seals once a second, so seal files and roll-ups follow the clock on slow clusters.
- A recovery failure counts as damage unless it is StoragePressure, DiskPressure or bad_alloc.
- The authority leader proposes EnablePositions as soon as it leads (no longer on demand): a
  follower answers W5 only once positions are enabled, so on-demand enabling left followers idle.
- Lead message (namespace-local-cursor done, merge it with DD1 and DD4): merged at Detent:e52e8d31;
  DD1 (Detent:5b46a601) and DD4 (Detent:14b29e24) were already in. Its pause note holds: the
  coordinator's pause ends only on close, a new cancellation, or wake().
- The agent's v1 generation identity flags go rather than map through D88: a W5 generation's
  incarnation is the authority's, so a D88-derived identity never matches (greenfield).
- A worker build failure is a retried candidate failure, recorded in status, not a stop: the
  worker lifetime test's settled case closes after observing it.
