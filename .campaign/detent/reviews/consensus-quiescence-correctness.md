# consensus-quiescence review: correctness

Commit: consensus:3b20d96888a990dda8e5123672a7c173d245f98d
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This is a re-review of the fixes for F1 and F2 from the review at consensus:a020e14.

- F1 is fixed. `campaign()` and `lead()` clear `resting_`, and `follower()` already did, so every
  role change into Follower, Candidate or Leader ends a rest.
- F2 is fixed as the lead ruled. A fenced group's Driver sends `Gone{term}` to its other members,
  and a follower of that replica in that term elects within its timeout.
- One new defect blocks. `Gone` takes the generic higher-term path in `handle(Receive)`, so a
  `Gone` from a non-leader one term ahead deposes a live leader at rest and forces an election
  (G1). The doc says such a `Gone` changes nothing. It also gets around the D7 leader-contact
  guard, which the same term carried by a `VoteRequest` cannot.

## Evidence I relied on

- 261121-012400-f56485: the mutant rest-survives-campaign (consensus:37a7a69) fails simulation_test.
  F1's test is live.
- 261121-012401-85a7ab and 261121-012403-b34836: the mutant fence-tells-nobody (consensus:2f0738c)
  fails `driver_test fence_tells_peers` and `groups_test driver`. The send side of F2 is live.
- 261121-012404-fce70e (3b20d96): `groups_test driver` passes. `quiescent fence: a follower
  elected 1107 ms after the fence: yes; node-gone reports 0`. `driver_test fence_tells_peers`
  passes 10 checks. Log read.
- 261121-012405-7903bb (3b20d96): my earlier probe. P1 passes in all four modes. P2 still prints
  NO, as it must: it is a Core-only probe with no Driver, so nothing sends `Gone`. The job's
  exit 1 is that expected P2 line.
- 261121-012405-a27269 (TSan, 3b20d96): `quiescence: at rest, LeaderGone, restarts, transfer and
  Gone: ok`, 300 seeds x 10,000 steps clean, `reopen_barriers` and `leader_gone` clean.
  `fence_tells_peers` and `groups_test` did not run under TSan. The new code runs on one loop,
  so I accept that.

## What I checked myself

Probe 261121-014000-f97fcf at 3b20d96 (light). The script is
`.scratch/detent/review-consensus-quiescence-correctness/probe2.cc`; rerun it with
`run.sh probe2.cc`.

- G1, a higher-term `Gone` from a follower:
  - A `Gone{2}` from follower 3 reached leader 1 at rest in term 1 and follower 2. The leader
    stepped down to term 2, and so did follower 2.
  - The group re-elected in term 3. Before the fix, a fenced replica sent nothing, so the leader
    was left undisturbed.
- G2, a stale `Gone`. After a transfer to node 2 (term 2), the old leader's `Gone{1}` and
  `Gone{2}` at node 3 left node 3 unchanged: following 2 in term 2.
- G3, `Gone` at awake followers. Both followers were awake (after a `Wake`) and both heard the
  fenced leader's `Gone`, one of them twice. There was one election, 900 units after the fence
  (election_max 1200), and one leader in term 2. One follower sent 4 vote messages and the
  other none. There was no term storm and no second election.
- G4, a same-term `Gone` naming a live leader (a delayed or forged one). The follower drops its
  leader, and its pre-vote wakes the leader. It is back at rest under the same leader in the same
  term after 920 units, with no election.
- G5, rest across role changes:
  - A follower at rest that hears a higher-term transfer vote ends up in term 2 with
    `resting 0`.
  - A leader at rest that fences keeps `resting 1`. `fence()` (core.h:616-619) does not clear
    it (N2).
- Code reading:
  - Who can send `Gone`. The source is `Receive::authenticated_peer`. The transport's per-frame
    rule (`member_of`) admits only members of the group, and the Core acts only when
    `leader_ == source`. A non-member cannot send one.
  - Tag 9. The decoder rejects an unknown kind with Corruption (codec.h, `default:`), and
    `receive_message` (host_transport.h:1597-1604) turns that into nullopt, a dropped frame. The
    connection is not aborted, so an older peer drops `Gone` cleanly. codec_test round-trips
    `Gone{7}` and rejects every truncation and a trailing byte.
  - Fenced node with no network. Its Control connection ends, so `on_node_gone` raises
    LeaderGone, unchanged from a020e14 (host_transport.h:1110-1113).
  - A4/A6. `report_fence` runs only when a group fences, and it is guarded by the existing
    `fence_seen` flag. The Group struct is unchanged. `step()` and `wakes()` are unchanged on
    the rest path, so the new send path adds no wake and no bytes at rest.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F7 | yes | defect | **A higher-term `Gone` deposes a live leader.** `Gone` goes through `handle(Receive)`'s generic term step (`changed`, `hard_.term = incoming_term; follower(0)`) before `receive_message(Gone)` checks the source and term. Any member's `Gone{T+1}` steps a leader at rest down and starts an election. That leader ignores a `VoteRequest{T+1}` from the same member under D7. The sender is a fenced replica whose in-memory term ran ahead, for example one that fenced while persisting its own campaign. Before this commit, a fenced replica sent nothing, so its higher term never reached the group. This contradicts quiescence.md ("from anyone else it changes nothing"). It costs liveness, not safety. | 261121-014000-f97fcf G1: leader 1 term 1 → stepped down, group re-elected in term 3; core.h:993-998, 1065-1067 | Handle `Gone` before the term step, as PreVoteRequest is handled: act only if Follower, `leader_ == source` and `term == hard_.term`, then return. Add to simulation_test `gone`: `Gone{term+1}` from a follower leaves the leader leading in `term`, and a stale `Gone{term-1}` from the old leader changes nothing. |
| F3 | no | gap | Carried, open. Random coverage of rest is thin: 18,594 node-steps at rest in the 150 quiescent seeds at 3b20d96 (20,375 at a020e14). Deterministic cases now cover transfer and `Gone`. | a27269 log | A rest-heavy seed mode, or deterministic cases per wake source. |
| F6 | no | gap | Carried, open. A4 counts only the leader's side. Follower rest at 1M groups, and a long rest with `stats().gone == 0`, are not counted. | quiescence-idle-cpu.md "What is not counted" | Optional: a follower-side idle row and a longer rest. |
| N1 | no | gap | `report_fence` notifies the Sink of `Fenced` before it sends the `Gone`s (driver.h:581-594). A host that forgets the group in its `Fenced` handler drops every `Gone`, because `HostTransport::send` drops frames for a group it does not store (host_transport.h:484-487). F2 then returns silently. `groups_test`'s sink ignores `Fenced`, so nothing tests this. | driver.h:581-594 | Send the `Gone`s before `Fenced`, or state the order in the Sink contract. |
| N2 | no | nit | `fence()` keeps `resting_`, so `GroupState::resting` reports a fenced former leader as resting. | f97fcf G5; core.h:616-619 | Clear `resting_` in `fence()`. |
| N3 | no | nit | No `Gone` seed in the checked-in decoder corpus. seed.cc writes one, but `fuzz/corpus/decoders` was not regenerated: its control seeds hold kinds 1-8 only, and the inserted seed shifts every later `seed-N`. codec_test covers the decoder. | corpus scan of 3b20d96 `fuzz/corpus/decoders` | Regenerate the seeds. |
| N4 | no | nit | `Gone` is sent once. It is lost to a full Driver inbox (driver.h:197) or a full control queue (host_transport.h:941), and then the followers stay at rest. A follower group closed at that moment is fine: it reopens with its election timer running. | code reading | None needed now. Name the limit in quiescence.md if it matters. |

F1 and F2 are closed (fixed, with mutant-killing tests). F4 is closed: the unreachable clause was
removed. F5 is closed: the comments now say rest is not required.

## Answers to the lead's questions

1. **Authentication.** Partly. The source is authenticated and must be this follower's leader.
   A stale lower-term `Gone`, or one from a non-leader in the same term, changes nothing (G2,
   simulation_test `gone`). A higher-term `Gone` from any member does depose (F7, blocking). A
   same-term forged or delayed `Gone` naming a live leader costs one pre-vote round and no
   election (G4). A fenced node with no network still produces LeaderGone through its
   Control connection's end.
2. **`Gone` while not at rest.** It behaves correctly: one election, one term step (pre-vote),
   and a duplicate `Gone` is harmless (G3). A candidate or leader ignores a same-term `Gone`.
3. **Tag 9.** An old decoder rejects it as Corruption, and the transport drops the frame without
   aborting the connection. codec_test round-trips `Gone` and rejects its truncations. The fuzz
   corpus lacks the seed (N3).
4. **F1.** Covered: `follower()`, `campaign()` and `lead()` all clear `resting_`, including a
   follower at rest stepping to a higher term (G5). Only `fence()` keeps the flag (N2,
   cosmetic).
5. **A4.** No regression: the send path runs only on a fence. The A4/A6 jobs ran at a020e14.
   `campaign status` lists them for a rerun at the head, which is the lead's gate.

## Not run

- No real-socket test of a host that tears down a group on `Fenced` (N1). No Linux run.
- No rerun of A4/A6 at 3b20d96, and no TSan of `fence_tells_peers` or `groups_test`.
- Witness groups at rest remain unprobed, as before.
- CI 44889402194 at 3b20d96 was still running when I checked.
