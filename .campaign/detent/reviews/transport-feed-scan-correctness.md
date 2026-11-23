# transport-feed-scan review: correctness

Commit: consensus:596a06548bcc7cd1b669582572ebd9510b5d0b1b
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This is fix round 1 (the previous review was CHANGES at consensus:5eacc91). F1 through F5 close.
The new in-order credit gets credit right on every path I read: the right group, never twice,
never lost, and dropped with its connection. One part of it is never run: `owe`'s merge, the code
that keeps a backed-up lane at O(groups) records, is reached by no groups case and not by A2.
Seeding a fault that drops merged credit passes everything. That is the one blocking finding (G1).
R7 holds. A2's slower credit return comes from the run conditions, not from the change.

## Evidence I relied on

- Suite 261118-003238-06723a, TSan 261118-003133-b42164 and A3 order -003134-30731c, identity
  -003133-c1416a, reconnect -003134-129143 and leftover -003134-38a9fa, all passed at
  consensus:303cad1. The only commits after it, 590308d and 596a065, change docs only. CI run
  44272560628 at 596a065 is green.
- F2: store-keeps-opens (b279376, 303cad1 minus `unopen` in `store`) is killed by `identity`
  (job 261118-003144-3730d0, exit 134 at the epoch3 assertion). F3: no-generation-clear (f2b3901)
  is killed by `reconnect` (261118-003140-29e975). I read both mutant diffs: each removes exactly
  the rule named.
- F1: exclusive A/B 261118-003330-778798 (D115). Receiver data frames are about 30% under main,
  and control frames 17-20% over, which the lead accepted.
- A2 261118-003133-4c4eb0: 3 of 3 passed, 298.2 B per group, credit back in 561-683 ms.

## What I checked myself

Scripts are in `.scratch/review-transport-feed-scan-correctness/` in the Detent home. Each job ran
at consensus:596a065.

- `owe-mutants.sh coalesce-reached coalesce-drops-credit`, job 261118-013424-64aee5 (light). One
  probe prints a line wherever `owe` merges records. It ran all 14 groups cases and A2 (`one 5 1
  10000`), and every one printed 0. The coalesce-drops-credit mutant keeps only the first
  record's bytes when merging. It passes 11 groups cases and A2. This is G1.
- `owe-mutants.sh owe-replaces reset-keeps-owed grant-ignores-generation`, job
  261118-033040-ab34b5 (light). It was still queued when this review was written (its first
  try, 261118-013426-4f977f, lost its runner). These mutants check whether the tests notice the
  owed path's other rules. They do not bear on the verdict: by reading, each of those rules is
  right (question 1).
- `credit-frames.sh`, job 261118-013433-b93ec8 (strict, beside 12 light holds). The bench is
  patched to print counts only. Hop data stream, interleaved base (main, consensus:edbb162),
  branch, branch, base:
  - Receiver credit frames per data frame: 0.43-0.49 on main against 0.55-0.90 on the branch.
  - Sender CPU: 1,693-1,798 ns per frame on main against 779-917 ns on the branch.
  - Receiver CPU, same/hop: main 698/866 and 706/865 ns, branch 496/610 and 529/668 ns.

  A2, interleaved 13b32d3 (credit owed in `Active`), branch, branch, 13b32d3: credit back in
  309.7, 404.2, 385.6 and 387.1 ms. Every arm sent 800,080 frames.

Answers to the lead's questions:

1. **Credit cannot go to the wrong group, twice, or be lost, by reading.**
   - Each `Owed` record carries the group of the frames it counts (host_transport.h:761-767,
     :801).
   - `repay` copies the record and advances `repaid` before it sends, so nothing is repaid twice
     (:621-643).
   - `owe` either takes the batch whole, when nothing is still owed, or appends it (:479-486).
   - The merge sums `bytes` and ORs `close` per group (:487-502).
   - A connection's end or start empties the lane's records (`reset`, :506-509).
   - A grant for an older generation, or one arriving with no link, is dropped (:809). This
     matches the sender, whose `reset` zeroes `outstanding`.

   A group forgotten mid-batch cannot cut a batch in two. `deliver` runs a whole batch on the
   group loop, and `forget` is a post to that same loop. The sender keeps its `Active` and takes
   the credit. Per-group credit and control-before-bulk hold: owed comes first, then control,
   then data (:588-614), and the sender's `min(bytes, outstanding)` is unchanged.

   The probes found three things. The merge is never run (G1). Credit frames roughly double,
   because the sender's round robin makes every run one frame long (N1). Under backpressure,
   owed can hold up to `max(4096, 2n)` credit frames ahead of a control frame (N2).
2. **R7 is sound.**
   - An accepted connection is handed to the peer's owner loop (links.h:187-196), so every
     connection of a node and lane lives on `loop_of_node`.
   - The tie-break lets a new link in only when the lane's link is gone or draining
     (links.h:84-87, :321).
   - `Connection::close` is called only at the peer's `close_notify` (connection.h:309), when the
     reader has stopped, or by `Links::retire` (links.h:254). HostTransport never calls `retire`
     (grep: only links_test calls it).
   - A batch carries the generation captured at `established` (host_transport.h:657), and one
     loop posts in order.

   So an older-generation batch never follows a newer one into a group loop, and no-current and
   F5's guard are equivalent today. Both stay as guards. This rests on HostTransport never
   retiring a link: a future caller of `retire` would make both live.
3. **Not a regression.** Interleaved in one strict job, 13b32d3 returned credit in 310 and 387
   ms and the branch in 404 and 386 ms, overlapping (b93ec8). My other A2 runs at the branch took
   425 and 363 ms (64aee5). In A2 each group sends one data frame per peer, so the credit frame
   count is the same in both designs (800,080 frames sent in every arm). The 561-683 ms in
   4c4eb0 is that run's conditions.
4. **F1-F5 close.** F1 is accepted on the exclusive A/B (D115). b93ec8 agrees: receiver data
   frames cost about 30% less than main and sender frames about 50% less. F2 is killed by
   3730d0. F3: `reconnect` plus 29e975 kill no-generation-clear, and no-current is equivalent
   (2 above). F4: the perf doc states one bound, `164 + 52P`, with its derivation beside the
   rows. F5: `opening` returns before the erase when `!current` (:828). The earlier F7 and F9 are
   fixed in the docs. F6 and F8 are unchanged and remain non-blocking.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| G1 | yes | gap | No test or bench reaches `owe`'s merge (the stable sort and per-group sum). It runs once a lane holds 4,096 unrepaid records while its connection cannot send, and it is what keeps that lane at O(groups) records. It carries credit and `closed`. A fault there loses a group's credit, which wedges the group at its window until the connection ends, or loses a `closed`. The coalesce-drops-credit mutant passes every case run and A2, and the reach probe printed 0 in all 14 cases and A2. | 261118-013424-64aee5; host_transport.h:479-503 | A case that backs the lane up past the floor: the receiver gets more than 4,096 runs (interleaved data frames of at least 2 groups) while its connection to that peer cannot send. One way is a RawPeer that stalls reading once its socket buffers fill. Then it reads, and the case checks that each group's credit returns in full (`uncredited` back to 0, and a frame needing the full window goes). Add coalesce-drops-credit to mutant_commit.py with a fails-without job. If a stalled reader cannot get past the kernel's buffers, a test-only floor option, or a white-box test of `owe` and `repay` against a per-group sum model, will do. |
| N1 | no | gap | One record per run of a group's frames in a batch, and the sender's round robin interleaves groups, so the receiver now sends about one credit frame per data frame. In the hop data stream that is 0.55-0.90 credit frames per data frame, against 0.43-0.49 on main's per-group map. Both ends' CPU still falls, so the net is a gain. The reverse direction carries about twice the credit bytes (17 B a frame). Neither D115 nor the perf doc states it. | 261118-013433-b93ec8; host_transport.h:761-767 | State it beside D115's numbers. Or merge runs per batch again, but only with numbers, since the per-batch `GroupTable` was the cost F1 removed. |
| N2 | no | defect | Owed records go before control frames. Before a merge, a lane can hold up to `max(4096, 2n)` records plus one batch, so a control frame can wait behind about 70 KB of credit frames once the connection drains. Main's coalesced map bounded that at one frame per group owing. It only arises under backpressure in the host's sending direction. | host_transport.h:485-488, :588-591 | Accept and note it, or merge at a lower floor. |
| N3 | no | nit | The repaid prefix of `Lane::owed` is freed only when every record is repaid or at a merge. A backlog that never empties and stays under `coalesce_at` keeps every record it has repaid. | host_transport.h:485, :621-627 | Erase the prefix in `owe` once `repaid` passes half the size. |
| N4 | no | nit | The perf doc gives F1's numbers from strict job 261118-002026-757230 (beside 26 light holds). D115 rests on exclusive job 261118-003330-778798, which the doc does not cite. | docs/perf/transport-idle-cost.md:168-177 | Cite 778798's rows (data same 607/625 to 465/470 ns, control 277/290 to 336/333). |

## Not run

- Job 261118-033040-ab34b5 (owe-replaces, reset-keeps-owed, grant-ignores-generation) was
  queued, not run, when this was written. Its result says whether the suite notices those rules.
  A survivor there is a test gap of G1's kind.
- No exclusive measurement of my own. b93ec8 is strict beside 12 light holds, one round per arm.
  Its CPU numbers corroborate D115 and do not replace it.
- TSan, the suite and A5 at 596a065: I relied on the jobs at 303cad1, since only docs changed
  after it.
- F6 (libstdc++ deque copies) on Linux: unchanged from the previous review.
