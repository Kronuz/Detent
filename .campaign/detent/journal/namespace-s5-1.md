# namespace-s5-1: journal

State: working
Quest: .campaign/detent/quests/namespace-s5-1.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

First build of the implementation (State: `Namespace` entries, enabling and drop
administrative entries, directory map, image DTAIMG02; node: administrative entries;
tests: test_namespace_positions, test_namespace_cluster, test_namespace_power_cut).

## Next

1. Build and run the three new tests and the authority suite locally (light hold).
2. Write docs/status/namespace-s5.md (how it works, contract).
3. Suite job (native) and pins; push the branch for CI's sanitizers (D54); crash model at the final head.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <job id at the current code> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. R1 No automatic enabling entry in S5.1. Turning positions on in a running daemon today breaks
   tests outside this quest's file set (`tests/detent_system/mixed.py` asserts an applied
   receipt's `revision == namespace_revision`) and today's delta wire, which gives records the
   group revision. The entry exists, is replicated and is tested; S5.2 proposes it with the first
   base, S6 moves the wire. `Node::propose_administrative` is the only way in.
2. R2 Administrative entries are their own payload (`DTAADM01`, carrying the semantic profile),
   not a new `Command` kind: they have no producer session, and the enabling entry is stepped
   (one namespace per budget update) like every other apply.
3. R3 Incarnations derive from the enabling entry's random nonce, the context, the directory
   position and the name (SHA-256, 16 bytes): every voter computes the same value, nothing extra
   is stored, and two restores of one backup with their own nonces never share an incarnation.
4. R4 A drop is in S5.1 (A2 needs delete then re-create). It is one change at the next position;
   the tombstone keeps that position and a re-creation continues from it, so positions under a
   name never repeat and old-incarnation preconditions conflict before W4 adds the incarnation to
   them. A drop names the incarnation it drops, so a retried drop never removes a re-creation.
5. R5 Live size is the canonical footprint `bytes_` already counts (key + encoded definition +
   16), kept per namespace from the start and recomputed from records at image decode; the
   namespace sizes sum to `logical_bytes()` (checked at decode).
6. R6 The change history captures a transaction only when its namespace's position equals the
   group revision; otherwise it records a gap and that namespace goes by snapshot until S6. Before
   enabling the two are always equal, so nothing changes today.
7. R7 Tombstones count toward `Limits::namespaces` and are kept (they hold a dropped name's last
   position). The image row grew to 224 bytes in the metadata bound.
8. R8 Image format `DTAIMG02`; `DTAIMG01` images are refused (D43).
9. R9 The cluster harness moved from `test_cluster.cc` into `cluster_harness.hh` so the A4 test
   reuses it instead of copying it.
