# dense-distribution-build: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Step 6: the tree builds and the suite passes on v2 (tests and system harness ported).

## Done

- Steps 2-4: `distribution::FrameInput` (test_frame_input); DiskStore cycles of named plans,
  selection and recovery per table entry (D8); the coordinator on one W5 session with per-namespace
  records; readers attach by name (C3) (Detent:6906fb4a).
- Step 5: managed.json `namespaces` or all, `retry_interval_ms`; v1 client, probe and
  PublicationSink removed; host readiness per namespace (C4) (Detent:db87768a).
- Merged dd4's final branch (Detent:5f21ccba) and the relay helper's branch (Detent:26ab90b4).
- Tests: test_namespace_coordinator (cold start, entries, blocks, W7, repair, drop, offline and
  resumed restart) replaces the v1 coordinator tests; managed process test on v2 (Detent:26ab90b4).

## Next

1. Port to the W5 fixture what the retired v1 tests covered: hash-once (D12.2), scrub damage to
   repair, prepared startup; a W5 fleet load for the measurement arms.
2. Step 6: system harness (managed.py, fleet) on v2; suite via `campaign verify`; push.
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

Nothing.

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
