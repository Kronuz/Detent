# namespace-s5-1: journal

State: waiting on jobs 261110-140332-98a69b
Quest: .campaign/detent/quests/namespace-s5-1.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Gate jobs at the head: native suite (crash model included) and pins; the branch is pushed for
CI's ASan, UBSan, TSan and platform matrix (D54).

## Next

1. Record native 261110-140332-98a69b (pins 261110-140332-333571 passed); set "ready for review".
2. CI did not run on the push: the branch's base (93ced6e) predates ci.yml's quest/** trigger
   (3b9ad2f). Rebase onto main after daemon-ready-leader lands (needed to land anyway), push,
   and record CI's sanitizer run.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | native 261110-140332-98a69b: `test_authority_namespace_positions` (`positions_start_at_stored_revision`: start at stored revision, advance only with own changes, image restart), `test_authority_namespace_cluster` (restart and snapshot install, exact positions per namespace) |
| A2 | native 261110-140332-98a69b: `test_authority_namespace_positions` (`incarnations_and_drop`: new incarnation on re-creation, old cursor answered `FullCopy`, retried drop of the old incarnation `Conflict`) |
| A3 | native 261110-140332-98a69b: `test_authority_namespace_positions` (`directory_stream`; `positions_start_at_stored_revision` enables over a store written before it) |
| A4 | native 261110-140332-98a69b: `test_authority_namespace_cluster` (three voters, one installed from a snapshot past the enabling entry and the drop, one restarted mid-run: identical entries, records and images) |
| A5 | native 261110-140332-98a69b: `test_authority_namespace_power_cut` (a cut at each of 352 storage operations; recovery keeps every acknowledged step and retries reach the uninterrupted state). Local mutant: images that forget tombstones fail it |
| A6 | native 261110-140332-98a69b: `test_authority_namespace_positions` (`incarnations_and_drop`: in-flight writes with old-incarnation preconditions conflict, a held root reads the dropped namespace whole, reclaim in bounded collect steps); the drop applies at one index on every voter (`test_authority_namespace_cluster`). Deferral: the agent's generations and `.lease` files for a dropped namespace belong to the agent store, S7 (docs/status/namespace-s5.md, "Left open") |
| pins | 261110-140332-333571 |

Messages: none listed by `campaign brief`.

## Result

Contract `namespace-positions` in `docs/status/namespace-s5.md`: `Namespace` entries (position,
incarnation, directory position, live size, tombstone), `NamespaceCursor` and `State::answer`, the
directory stream, the enabling entry and the namespace drop as administrative entries
(`Node::propose_administrative`), image `DTAIMG02`. Nothing proposes either entry yet (R1).
Open: agent side of a drop (S7); tombstones kept and counted (S6 can cut them).

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
   position and the name (SHA-256, 16 bytes): every voter computes the same value, decode checks
   each live one against it, and two restores of one backup with their own nonces never share an incarnation.
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
   position). The image row grew to 240 bytes in the metadata bound; it carries the incarnation, since a
   tombstone keeps the dropped one, not its derivation at its new directory position.
8. R8 Image format `DTAIMG02`; `DTAIMG01` images are refused (D43).
9. R9 The cluster harness moved from `test_cluster.cc` into `cluster_harness.hh` so the A4 test
   reuses it instead of copying it.
