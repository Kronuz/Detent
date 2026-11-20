# dense-distribution-build: journal

State: blocked on lead: relay staleness ruling (Blocked on); waiting on jobs 261120-085935-8d23e7 (A1) and CI (A2) at Detent:d9668727
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Step 6: suite and CI at Detent:d9668727; relay staleness awaits the lead.

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

## Next

1. Port to the W5 fixture what the retired v1 tests covered: hash-once (D12.2), scrub damage to
   repair, prepared startup; a W5 fleet load for the measurement arms.
2. A selected entry whose bytes are damaged (startup checks presence only, C2) refuses recovery
   on each round: base_of fails, and a full copy at the same incarnation also asks base_of. The
   store should fail the entry (as startup does for a missing file) so a fresh copy replaces it.
3. `distribution::UpdateCache` is unused; ACTORS.md and RELAY_ARCHITECTURE.md describe bundles.
4. Step 7 (former DD3): full copy as a page transfer through DD1's receiver; a 100K cold agent
   runs no packer.

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

Lead ruling, relays and sealed history (the cause of test_system_managed_relay[_ipv6] failing
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

For the lead, outside this file set: .github/workflows/tls-fixture-diagnostics.yml is left at
main's version, which names `test_delivery_coordinator_subscription-relay-http-500000`, a test
this branch retires (retarget it to test_namespace_coordinator, or delete it). README.md still
lists `detent::local_export`, which this branch removes. .github/workflows/ci.yml carries
namespace-local-cursor's removal of test_local_view from the reader job; it stays.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-081334-e4f6a8: 205/205 at Detent:501f9c4d (relay system tests flaky under load: see Blocked on; 261120-080129-538d87 failed them at Detent:410242d4) |
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
- `delivery::read_private_configuration` duplicates the relay's new
  `detent::internal::read_private_configuration`; left for the lead (the helper's edit was denied).
- The authority leader proposes EnablePositions as soon as it leads (no longer on demand): a
  follower answers W5 only once positions are enabled, so on-demand enabling left followers idle.
- Lead message (namespace-local-cursor done, merge it with DD1 and DD4): merged at Detent:e52e8d31;
  DD1 (Detent:5b46a601) and DD4 (Detent:14b29e24) were already in. Its pause note holds: the
  coordinator's pause ends only on close, a new cancellation, or wake().
- The agent's v1 generation identity flags go rather than map through D88: a W5 generation's
  incarnation is the authority's, so a D88-derived identity never matches (greenfield).
- A worker build failure is a retried candidate failure, recorded in status, not a stop: the
  worker lifetime test's settled case closes after observing it.
