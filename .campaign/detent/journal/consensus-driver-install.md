# consensus-driver-install: journal

State: waiting on jobs 261120-193730-660db9 261120-193735-f56dd7; blocked on the lead: msg-180
Quest: .campaign/detent/quests/consensus-driver-install.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

At consensus:4d5b6e2 (5 commits on main 8801b99); 4d5b6e2 changes only the simulation's repair
check and a DRIVER.md sentence. Built at consensus:c0838ff:
driver.h install surface, worker.h snapshot_image + activation-wait fix, host_transport.h
send_transfer/is_transfer, driver_install_test (lagging, rejected, stale, restart, crash),
driver_simulation installs, groups_test install over the real host transport. Evidence jobs queued.

## Next

1. On the lead's ruling (msg-180): update the Sinks in test/driver_test.cc,
   test/driver_close_test.cc, test/wave_open_test.cc (rename ActivateInstall, add SendTransfer
   and ValidateImage); CI does not compile them until then.
2. Read A4's jobs 261120-193730-660db9, -193735-f56dd7 (at 4d5b6e2). A1-A3 passed at c0838ff.
3. Merge consensus-quiescence, consensus-transport, node-admission (consensus:c669067) when they
   land; rerun the jobs (`campaign job rerun`).
4. Push; CI macos-arm64 + TSan/ASan for A6. After consensus-quiescence merges: a follower's
   orphaned incoming image ends only when run_one sees time pass; check that a quiescent group
   still steps (arm a wake while an install is open) so its repair is not held forever.

## Blocked on

The lead (msg-180): extend the file set with test/driver_test.cc, test/driver_close_test.cc,
test/wave_open_test.cc. The contract replaces the Sink's ActivateInstall with ActivateImage and
adds SendTransfer and ValidateImage to the sink concept; those three tests' explicit Sinks stop
compiling without a mechanical update. Recommended: extend. Alternative: make the install
notices optional (if constexpr), two shapes.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | jobs 261120-191737-e1518e (lagging), -191739-f8b0b5 (stale+restart), -191753-7348c5 (groups install over the host transport); fails-without -191744-5b41ef (no-transfer-out), -191746-d10632 (activation-wait-maintains), -191754-1b07f7 (no-transfer-out, groups) |
| A2 | job 261120-191740-5424de (rejected); fails-without -191749-bef99d (failed-validates) |
| A3 | job 261120-191742-f0f7eb (crash at every disk op, 24 cuts: 19 old, 5 new locally); fails-without -191751-b901bb (activate-on-validated) |
| A4 | jobs 261120-193730-660db9, -193735-f56dd7 (32 seeds each, at 4d5b6e2, queued). At c0838ff 261120-191756-1ba6e8 failed seed 15: a repair pending after healing (ruling 8); its summary: 35 SnapshotNeeded, 176 frames, 16 validated (1 refused), 15 activated. Seed 15 passes at 4d5b6e2 locally |
| A5 | [artifact] consensus `docs/DRIVER.md` section "Install" (consensus:c0838ff); contract sent to Xapiand as msg-187 |
| A6 | pending: CI after the msg-180 ruling |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

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
6. worker.h: an installed image awaiting activation is no longer runnable and no longer runs
   reclaim maintenance each turn (a hot loop with inline IO; mutant activation-wait-maintains).
7. Mutants live in test/transport/mutant_commit.py (in the file set), with full paths.
8. Simulation seed 15: node 1's repair waited behind an incoming image whose source (node 2)
   crashed; the Worker answers Repair Busy while it holds a snapshot, and the channel ends the
   receive at snapshot_timeout (60 s there), longer than the 1000-round repair check. The check
   now runs 1000 + 2 x snapshot_timeout / 4 rounds, as healing does; DRIVER.md states that a
   follower's repair and checkpoint wait behind an incoming image until its timeout. Traced with
   temporary dumps of the Worker phase (Application, Incoming) and the channel's receive
   (connected, not final), not committed.
