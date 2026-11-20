# dense-distribution-build: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Step 1: relay on v2 only (helper agent on branch `quest/detent/dense-distribution-build--relay`,
worktree ~/.scratch/detent/ddb-relay/wt): bundles, the v1 session routes, the per-bundle
Coordinator and the PublicationSink go; the relay is its namespace mirror.
Step 2 (me, in parallel): the delivery client's v1 request, one-shot `receive`, v1 sessions,
`DeliveryMode` and poll go; `distribution::FrameInput` mints a one-namespace ValidatedSnapshot
(full copy) or ValidatedDelta (entries, blocks; grouped by revision) from W6 records, so the
paged builders take W6 frames unchanged.

## Next

1. Step 3: the agent coordinator on W5/W6 (C1): one session, one FramePreparer, per-namespace
   state as records (name, W1 cursor, base, failed); each response builds one plan per namespace
   on the worker and publishes them in one AgentStore cycle (D8); Dropped frames drop entries.
2. Step 4: DiskStore and recovery per entry (D8, C2, C3): `begin` takes N plans; selection and
   recovery by name; bases recovered on demand under a bound; readers find their entry by name.
3. Step 5: managed config and host: `namespaces` or `all`, no group/epoch/scope/principal/
   reset_from/delivery_mode/poll; readiness per namespace (C4).
4. Step 6: system harness (managed.py, fleet) on v2; suite via `campaign verify`; push.
5. Step 7 (former DD3): full copy as a page transfer through DD1's receiver; a 100K cold agent
   runs no packer.

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
