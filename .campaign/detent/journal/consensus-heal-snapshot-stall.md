# consensus-heal-snapshot-stall: journal

State: waiting on jobs 261120-011552-f33105 261120-011554-819825 261120-011554-ca003a 261120-011554-ff1844 261120-011554-c86a94 261120-011554-138f3d 261120-011601-4154cf 261120-011603-79ea73
Quest: .campaign/detent/quests/consensus-heal-snapshot-stall.md (the lead's; never edit it)

## Doing

Head consensus:47e7005, pushed. Changed after consensus:5a8685c: test/worker_test.cc only (one loop predicate, consensus:47e7005); include/consensus/core.h, ARCHITECTURE.md, CHANGELOG.md and test/consensus_test.cc unchanged, so the correctness review's coverage of core.h stands.

## Suite failure at 5a8685c (job 261120-001946-6b43d4)

1. Which test: `binary_transfer_integration(65553, mode 2, sync)` in consensus_worker (instrumented locally; the failure line prints no parameters).
2. Attribution, consensus_worker x4 each: head 5a8685c 4/4 fail (261120-003741-f752f3); main a8e867b 0/4 (261120-003747-00aef3). Deterministic: the fix causes it; not a flake.
3. Cause: a turn race in the test's exit predicate, not a protocol defect. Mode 2 drops the receiver's Installed result and jumps the leader's clock 60,040 ms; the leader times the install out, steps down (no quorum contact on its clock) and is re-elected in term 2 with a no-op at 3. The loop exited only when all three nodes stood at applied 2 in the same turn, i.e. only if the receiver applies 2 before the leader applies 3. Traces of both cores (all messages to and from node 2 and 3): identical message sequences except the refusal round trip the fix adds at the heal (append prev 1 refused hint 1, then SnapshotNeeded); after the re-election the same messages, one turn apart in the leader's scheduling. Main: receiver applies 2 a turn before the leader applies 3; head: same turn, so the predicate never holds and the run ends 3/3/3 with the exact application state (image + "/tail" on every node).
4. Sweeps, local probe build (receiver opened 0..19 turns late; leader jump 60,040 + 0..38): the old predicate holds on main 20/20 and 20/20, on head 3/20 and 0/20; the new predicate holds 40/40 on each core with 0 check failures.
5. Change, consensus:47e7005: the loop waits until each node has applied 2 or past it (`>= 2`); the final check of exact application state (receiver == image + "/tail", leader and healthy equal, no source held) is unchanged.

## Next

1. Read the jobs: suite 261120-011552-f33105; consensus_worker x4 at 47e7005 (261120-011601-4154cf) and 47e7005's test with main's core.h (261120-011603-79ea73, supporting); A1-A3 reruns at 47e7005 (819825, ca003a, ff1844, c86a94, 138f3d); CI on 47e7005.
2. Paste `campaign brief`'s evidence table.

## Blocked on

Nothing. The 60 s is not a design bound for this case: snapshot_timeout bounds an install whose source never progresses (ARCHITECTURE.md, Sender flights); here no install was needed. Fixed in the Core, no wire change.

## Cause (A1)

Reproduction: Xapiand's Driver simulation at consensus:98298fb (branch quest/xapiand/consensus-sim-loops; its core.h is main's), `SIM_DRIVER_SEEDS=1 SIM_DRIVER_TRACE=1 consensus_simulation_test 0 1000 <seed>`, seeds 4270, 6197, 8247: deterministic (5 identical traces), at 1,000 steps (3,000 steps does not hit it). Each emits one SnapshotNeeded at heal round 0-1 and heals only by rounds 14,881 / 13,154 / 10,498. Seed 399 is unrelated (the 3,000-step regression seed the same binary replays).

Core state at the stall (seed 4270, group 412174842685, leader node 1 term 4, peer 2 a data voter; Core instrumented in scratch):
1. now 2035: peer 2 refuses an append at previous 5 with hint 1 (empty log): next 1, matched 0.
2. now 2076-2343: the append at previous 0 reaches peer 2 (it commits 1..2, holds 1..7), but no reply reaches the leader; each rpc_timeout `reset_flights` puts next back at matched + 1 = 1.
3. now 2353: the leader checkpoints at base 2; the flight then times out with next 1 <= base 2 and nothing in flight, so `replicate` asks for an install (SnapshotNeeded at base 2) though no answer showed peer 2 lacks the base.
4. The simulated host never supplies a source; while AwaitSource, `replicate` sends only empty keepalives (prev 0; replies ignored by design), so nothing probes peer 2's log.
5. now 62353 (2353 + snapshot_timeout): TimedOut release with a probe at base + 1; the append succeeds and peer 2 commits.

SnapshotNeeded goes unanswered because the simulated host sends no images. The leader waits because the Core took "next <= base" to mean "the peer lacks the base" when a flight timeout and its own checkpoint, not the peer, put next there; a pending install then owns the peer until released. A production host would build and ship a full image (answered CaughtUp or Installed) to a peer that needed one append.

Core reproduction: `heal_after_checkpoint_past_stale_match` in test/consensus_test.cc (Network fixture with a one-way cut, compact timing 20/40): cut 2->1, replicate c3-c8, let flights time out, checkpoint past 2's stale match, time out again, heal. Fails at consensus:910925a (test-only commit).

## Fix (A2)

consensus:95e15f3, include/consensus/core.h: `Peer::lacks_base`, set by a refusal that puts next at or below the base, cleared by any append sent and by an image installed or released with a probe. `replicate` starts an install only when it is set; otherwise it appends at the base, and a refusal there starts the install one round trip later. Bound: after the heal the peer catches up within rpc_timeout (its outstanding flight's timeout), which is 2 heartbeats at the simulation's timing (20/40) and 5 at the library default (100/500); the same bound as for any lost flight. The A2 test asserts 2 heartbeats at the compact timing.

## Evidence

Jobs at head 5a8685c: A1 261119-232821-ad2f96 (builds 98298fb: expect-stall); A2 261119-232822-da09d2 (test passes), 261119-232822-bff66e (head's core.h in 98298fb's sim: expect-heal); A3 261119-232822-71c928 (head's core.h in c08afe7's sim, 4,000-round heal: 10,000 seeds x 1,000 steps, 2,000 x 3,000), 261119-232823-89d14a (main's sim: 1,000 Core seeds x 10,000, 100 Driver seeds x 3,000), 261119-232829-68d538 (suite); CI on 5a8685c (TSan).
Passed: 261119-231702-89d08c (fails-without at test-only 910925a: the heal test fails), 261119-231700-6abdcf (fails-without at c08afe7: seed 4270 fails the 4,000-round heal); supporting, run at 98298fb itself: 261119-231659-1c3890 (1 SnapshotNeeded each; heal rounds 14,881 / 13,154 / 10,498), 261119-231718-ff0cc8 (with the fix: 0 SnapshotNeeded; heal rounds 7 / 40 / 40). Script: /Users/kronuz/.scratch/detent/consensus-heal-snapshot-stall/jobs/heal-seeds.sh.

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |

## Result

Pending the jobs. ARCHITECTURE.md (compacted follower paragraph) states the install rule: consensus:70ddd54; CHANGELOG.md 2026-11-19 Fixed: consensus:5a8685c. Xapiand's 98298fb (heal outlasting 2 x snapshot_timeout) can return to 4,000 rounds once this lands.

## Rulings I made

4. test/worker_test.cc is outside the quest's file set (core.h, consensus_test.cc, ARCHITECTURE.md, CHANGELOG.md); the lead asked for an order-assuming test to be updated, so I changed its one predicate. The lead adds it to the file set.

1. One per-peer flag checked in `replicate`, rather than clamping next at each site that moves it without evidence (flight timeout, checkpoint, install outcome): one decision point. A refusal's evidence survives a later checkpoint: by log matching, a peer lacking the leader's entry at i lacks every later one until the leader sends it more.
2. A refusal that leads to an install still installs at once; a Rejected, Stale or Unavailable release keeps the flag, so those retries still request a new image, as the existing tests require.
3. The A2 bound is measured in the compact profile (heartbeat 20, rpc_timeout 40), the simulation's timing.
