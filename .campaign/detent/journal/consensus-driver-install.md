# consensus-driver-install: journal

State: fixing R1: waiting on jobs 261121-013716-103b97 261121-013719-2f4f8c 261121-013721-c624b5 261121-013723-a8b6ab 261121-013725-4b7677 261121-013727-e388e6 261121-013729-ec46d2 261121-013732-2cca08 261121-013733-6374c5 261121-013810-bf372c 261121-013810-c031ca 261121-013811-92c79d 261121-013811-3be523 261121-013811-05e1ad 261121-013811-df4da1
Quest: .campaign/detent/quests/consensus-driver-install.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

At consensus:740453b: merge 017c918 of main bb961b3 (consensus-faultfs, clean; the three Sink
files differ from main only in their Sink definitions), then 740453b, the worker_test fix.
The quest: driver.h install surface, worker.h snapshot_image + activation wait, host_transport.h
send_transfer/is_transfer, driver_install_test (lagging, rejected, stale, restart, crash),
driver_simulation installs, groups_test install over the real host transport.

## Next

1. Read the jobs at consensus:887a88b (State): orphan 013716-103b97 passes, its mutant
   orphan-no-wake (5acecc2) 013719-2f4f8c fails, lagging/stale+restart/groups/rejected/crash/
   simulation and the reruns pass; CI macos-arm64 at 887a88b. Then
   `campaign accept consensus-driver-install correctness --confirm R1=<orphan job> ...`.
2. When consensus-quiescence lands: merge consensus main into the quest branch (the tool asks),
   rerun every job and push. Land only from that merge.
3. Merge consensus-transport and node-admission (consensus:c669067) when they land.

## Review (correctness, CHANGES at 740453b)

- R1 (blocking, taken): consensus:b695665 on the quest branch, after 8c4a62f merges
  consensus-quiescence at 3b20d96 (the earlier b9e9fac on wip/install-on-quiescence is superseded). Driver::arm wakes a group holding an
  incoming image at the channel's deadline (last frame taken + snapshot_timeout), and a timeout
  later while a whole image waits on its answer. Test `driver_install_test orphan`: quiescent,
  the leader's transfer stops after its first chunk and the leader goes; at rest node 3 drops
  the image within snapshot_timeout and repairs at once. Locally, without the wake both "arms its
  receive timeout" and "repairs at once" fail: the reviewer's hold is real.
- R2 (taken): the crash case counts cuts whose recovery activates a durable, not yet activated
  image. Job 261121-005959-4cf07a: 26 operations, 24 cuts, 19 old, 5 new, 1 activated at
  recovery, 0 failures. The case now requires such a cut (consensus:887a88b).
- R3 (taken): DRIVER.md says a whole image waits for its answer however long, and that a resting
  group wakes for the receive deadline.
- R4 (follow-up): validation and activation tokens restart per Worker; an answer for a closed
  group delivered after the same id reopens could match. Needs a contract ruling (state it, or
  salt tokens per incarnation); not a trivial change.
- R5 (follow-up): one frame queue per group, so a second sender's Busy Begin holds the current
  sender's Chunks until the receive timeout. Needs two concurrent leaders; per-peer queues.

## Blocked on

Nothing.

msg-1 (answered): merged consensus-quiescence 3b20d96 into the quest branch as consensus:8c4a62f
(tree = the earlier 135a269 resolution plus 3b20d96's own delta, checked by diff), then R1
(b695665) and R2 (887a88b) on top; pushed. No visitor in this quest's files is exhaustive over
Message, so Gone needs no arm here.

msg-180 (answered): the lead ruled required install notices, one shape, and added
test/driver_test.cc, test/driver_close_test.cc, test/wave_open_test.cc to the file set.
consensus:5982871 updates only their Sink definitions: SendTransfer and ValidateImage stubs,
ActivateImage in place of ActivateInstall (driver_test records the boundary, since the notice
borrows its reader). The quiescent-group risk stays under Next.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261120-225736-8a7fed, 261120-225738-b62375, 261120-225744-fa0a56; fails without: 261120-190408-045251, 261120-191744-5b41ef, 261120-191746-d10632, 261120-191754-1b07f7, 261120-225747-e54d29 |
| A2 | 261120-225740-b2bdb1; fails without: 261120-191749-bef99d |
| A3 | 261120-225742-c5e61e; fails without: 261120-191751-b901bb |
| A4 | 261120-225748-45cc05, 261120-225750-c12778 |
| A5 | [artifact] consensus `docs/DRIVER.md` section "Install" (consensus:740453b); contract sent to Xapiand as msg-187 |
| A6 | `ci:macos-arm64` (CI run 44877138480 at consensus:740453b, green on every platform, TSan and ASan included); 261120-225730-f34285; fails without: 261120-225734-b342d4 (worker_test at 017c918: "held ordinary output retains activation completion while reclamation continues") |

## Result

The consensus Driver installs snapshots per group over the host transport (consensus:740453b,
branch quest/detent/consensus-driver-install). Contract: docs/DRIVER.md "Install"; frames on
Bulk: docs/mechanisms/host-transport.md. Sink notices SendTransfer, ValidateImage, ActivateImage
(replacing ActivateInstall); calls transfer, validated, validation_failed. worker.h gains
snapshot_image and stops counting an image awaiting activation as runnable (a hot loop with
inline IO). Tests: test/driver_install_test.cc (lagging, rejected, stale, restart, crash at
every disk operation: 24 cuts, 19 old and 5 new replicas), the groups_test install case over
real TLS, and installs in driver_simulation (64 seeds; at c0838ff's 32: 35 SnapshotNeeded, 176
frames, 16 validated, 1 refused, 15 activated). Open: the quiescent-group orphaned image (Next 3).

## Rulings I made

1. Transfer session identities are fixed per incarnation ("DXFR" + incarnation), the same at both
   ends; the host transport has no per-group session exchange. Freshness rests on the
   SnapshotKey (term, transfer token, boundary) and the channel's receive timeout, which is the
   group's Timing::snapshot_timeout. A lost frame stalls a transfer until the leader's
   snapshot_timeout, then it starts over (DRIVER.md says so).
2. The Driver consumes the readback's data chunks itself (CRC-checked by the verifier) and hands
   the application one ValidateImage at the verified EOF with a reader of the whole image,
   borrowed for the Sink call, as recover's Restore is. ActivateImage carries the same reader.
3. validation_failed takes no reason: a refusal does not fence and nothing records one.
4. Install state (the SnapshotChannel and queues) is created at the group's first transfer frame
   or source, and freed at close (Y9: an idle group holds none).
5. send_transfer takes (node, peer, frame), not the notice, so host_transport.h does not include
   driver.h.
6. worker.h: an installed image awaiting activation is no longer runnable, and its activation
   branch blocks instead of running reclaim maintenance (a hot loop with inline IO; mutant
   activation-wait-maintains). Under held ordinary output it still reclaims, as worker_test
   requires (consensus:740453b; 5982871 had dropped that too and CI failed). The code was fixed,
   not the test: DRIVER.md says nothing about reclamation while an activation waits.
7. Mutants live in test/transport/mutant_commit.py (in the file set), with full paths.
8. Simulation seed 15: node 1's repair waited behind an incoming image whose source (node 2)
   crashed; the Worker answers Repair Busy while it holds a snapshot, and the channel ends the
   receive at snapshot_timeout (60 s there), longer than the 1000-round repair check. The check
   now runs 1000 + 2 x snapshot_timeout / 4 rounds, as healing does; DRIVER.md states that a
   follower's repair and checkpoint wait behind an incoming image until its timeout. Traced with
   temporary dumps of the Worker phase (Application, Incoming) and the channel's receive
   (connected, not final), not committed.
