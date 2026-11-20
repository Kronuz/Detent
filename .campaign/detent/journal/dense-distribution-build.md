# dense-distribution-build: journal

State: waiting on jobs 261120-071229-c25fa5 (A1 suite) and CI (A2) at Detent:2bc66eb4
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Step 6: suite and CI on v2 at Detent:2bc66eb4.

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

## Next

1. Port to the W5 fixture what the retired v1 tests covered: hash-once (D12.2), scrub damage to
   repair, prepared startup; a W5 fleet load for the measurement arms.
2. `distribution::UpdateCache` is unused; ACTORS.md and RELAY_ARCHITECTURE.md describe bundles.
3. Step 7 (former DD3): full copy as a page transfer through DD1's receiver; a 100K cold agent
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

Nothing. For the lead: .github/workflows/tls-fixture-diagnostics.yml (outside this file set) is
left at main's version, which names `test_delivery_coordinator_subscription-relay-http-500000`,
a test this branch retires. Options: retarget it to test_namespace_coordinator (recommended), or
delete the workflow.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
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
- A worker build failure is a retried candidate failure, recorded in status, not a stop: the
  worker lifetime test's settled case closes after observing it.
