# consensus-heal-snapshot-stall: journal

State: waiting on jobs 261119-232059-0f5a1b 261119-232059-943668 261119-232100-da5cee 261119-232100-90a070 261119-232100-9d53be 261119-232101-ed03fd 261119-231700-6abdcf 261119-231702-89d08c
Quest: .campaign/detent/quests/consensus-heal-snapshot-stall.md (the lead's; never edit it)

## Doing

Jobs queued for A1-A3 at head consensus:70ddd54 (ids in State); CI on the pushed branch (linux-tsan, ASan/UBSan, matrix).

## Next

1. Read the jobs and CI; paste `campaign brief`'s evidence table; then ready for review (correctness).

## Blocked on

CHANGELOG.md, held by consensus-dns-notes (the lead adds it to this file set when that lands). Entry to commit then:

> The leader starts an install only after a peer refuses an append at or below its base. A next index there for any other reason, a flight timeout or the leader's own checkpoint, first appends at the base, so a peer that already holds the entries catches up within rpc_timeout of a heal instead of waiting out snapshot_timeout (60 s).

Otherwise nothing. The 60 s is not a design bound for this case: snapshot_timeout bounds an install whose source never progresses (ARCHITECTURE.md, Sender flights); here no install was needed. Fixed in the Core, no wire change.

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

Jobs at head 70ddd54 (rerun of the 95e15f3 ones): A1 261119-232059-0f5a1b (head, builds 98298fb: expect-stall), 261119-231700-6abdcf (fails-without at c08afe7: seed 4270 fails the 4,000-round heal), 261119-231702-89d08c (fails-without at 910925a); supporting, ran 98298fb itself: 261119-231659-1c3890 passed (1 SnapshotNeeded each; heal rounds 14,881 / 13,154 / 10,498). A2 261119-231702-89d08c, 261119-232059-943668 (test passes at head), 261119-232100-da5cee (head's core.h in 98298fb's sim: expect-heal); supporting 261119-231718-ff0cc8 passed (0 SnapshotNeeded; heal rounds 7 / 40 / 40). A3 261119-232100-90a070 (head's core.h in c08afe7's sim, 4,000-round heal: 10,000 seeds x 1,000 steps, 2,000 x 3,000), 261119-232100-9d53be (main's sim: 1,000 Core seeds x 10,000, 100 Driver seeds x 3,000), 261119-232101-ed03fd (suite); CI on 70ddd54 (TSan). Script: /Users/kronuz/.scratch/detent/consensus-heal-snapshot-stall/jobs/heal-seeds.sh.

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |

## Result

Pending the jobs. ARCHITECTURE.md (Snapshots, compacted follower paragraph) states the install rule: consensus:70ddd54. CHANGELOG.md waits (Blocked on). Xapiand's 98298fb (heal outlasting 2 x snapshot_timeout) can return to 4,000 rounds once this lands.

## Rulings I made

1. One per-peer flag checked in `replicate`, rather than clamping next at each site that moves it without evidence (flight timeout, checkpoint, install outcome): one decision point. A refusal's evidence survives a later checkpoint: by log matching, a peer lacking the leader's entry at i lacks every later one until the leader sends it more.
2. A refusal that leads to an install still installs at once; a Rejected, Stale or Unavailable release keeps the flag, so those retries still request a new image, as the existing tests require.
3. The A2 bound is measured in the compact profile (heartbeat 20, rpc_timeout 40), the simulation's timing.
