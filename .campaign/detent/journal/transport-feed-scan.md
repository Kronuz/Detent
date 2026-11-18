# transport-feed-scan: journal

State: fix round 2 (G1) at consensus:4b88ad2, pushed; waiting on jobs 261118-045307-0627ca (backlog), 261118-034609-0c0786 (fw coalesce-drops-credit), 261118-034611-6144e1 (fw, review script), 261118-034613-a94a25 (suite), 261118-045307-857f92 (A2).
Quest: .campaign/detent/quests/transport-feed-scan.md (the lead's; never edit it)

## Fix round 2 (session 5): G1

- G1: no case reached `owe`'s merge. Cause: the writer's flush loops without yielding while
  SSL_write succeeds, so owed records pile up only when the socket is full (the peer not
  reading). New case `backlog` (consensus:4b88ad2): RawPeer (now sums credit per group, and
  sleeps 150 ms after each frame over 1 MiB while `slow`) opens 6,000 groups on host 2, reads
  slowly while host 2 sends it 6 x 4 MiB, and sends 3 data frames per group meanwhile; asserts
  each group's credit equals its expected bytes, no closed, and credit frames < 18,000 (merged).
  Local run: 575,980 of 575,980 B, 6,000 of 6,000 groups whole, 9,436 credit frames; node 1 had
  read 2 of 6 bulk frames when all 18,000 arrived. No product change: the merge is right.
- Mutant `coalesce-drops-credit` added to mutant_commit.py (91da6ea, on no branch).
- Jobs at 4b88ad2: plain backlog 261118-045307-0627ca; fails-without at 91da6ea
  261118-034609-0c0786; the reviewer's owe-mutants.sh (copy in the code worktree's
  .scratch/g1/: `--cases`, stacked `a+b`, exit 1 when killed, 126 on setup failure) with
  `coalesce-reached coalesce-reached+coalesce-drops-credit` on backlog, fails-without,
  261118-034611-6144e1; reruns suite 261118-034613-a94a25, A2 261118-045307-857f92.

## Fix round 1 (session 4)

Review .campaign/detent/reviews/transport-feed-scan-correctness.md. Lead msg: every job at 13b32d3
passed (TSan 80ef1e, A2 49251f, A5 240b25, A3 eb4940, suite 7f5dbb, A6 x100 4b73fc).
- F1: consensus:48d04eb. A batch's credit is a vector (a run of one group coalesced), not a
  GroupTable per batch; the connection loop owes it in `Lane::owed` (24 B `Owed`, own FIFO
  `owing`), so no `Active` is built for receiving. Strict A/B rerun of 0701eb: 261117-235520-8aad49.
- F2: killed at 13b32d3: 261117-224140-de7732 aborts on identity's new step assertion
  (groups_test.cc:1234, `closed_again && !received(ok, "epoch3")`: "refused NO, delivered 20 of
  20"). At 48d04eb: 261117-235623-f9fa6f.
- F3: new case `reconnect` (RawPeer, shared with `leftover`): open on connection 1, end it, frame
  on connection 2 without the open must be `unopened` and closed back. Mutants added:
  no-generation-clear 261117-235604-889a1c, no-current 261117-235613-498ed0. Plain:
  reconnect 261117-235543-7b173e, leftover 261117-235554-891b65.
- F1 step 1 at 48d04eb, A/B 261117-235520-8aad49 (strict, beside 15 light holds): data same
  1515/1660 ns vs main 982/954 (1.6x; was 2.5x), control 622/627 vs 506/524. Not enough.
  Step 2 consensus:303cad1: owed is a per-lane vector of 24 B records taken whole from the
  batch, repaid in order, freed when repaid; merged per group when it doubles (>= 4,096) while
  the connection cannot send. No per-group table on receive. A/B 261118-002026-757230.
  Diag profile (host 2's threads' self samples, .scratch/prof/ in the code worktree) at
  48d04eb: 261118-001853-9bb0ba (never evidence).
- F1 at 303cad1, strict A/B 261118-002026-757230 (beside 26 light holds), base/branch/branch/base:
  data same 1041 / 758 / 531 / 747 ns, hop 1186 / 936 / 660 / 866: branch at or under main.
  Control same 513 / 605 / 429 / 331: still about 20-30% over main. Diag 9bb0ba (at 48d04eb)
  put the owed table's erase/operator[]/owe at 704 samples (gone at 303cad1) and deliver's
  own lines 743/751 (`stored.find`, `groups.contains`) at 376 vs main's whole deliver 120: the
  two GroupTable lookups per frame are the control-path cost. Deciding exclusive A/B:
  261118-003330-778798.
- Reruns at 303cad1: A2 4c4eb0 (298.2 B, 3 of 3; credit back 561-683 ms, was 290-438),
  A5 eee91a, TSan b42164, identity c1416a, reconnect 129143, leftover 38a9fa all pass; order
  30731c, scan bead1c, suite 06723a pending. Mutants all killed (exit 134): no-generation-clear
  29e975, store-keeps-opens 3730d0, ring-at-front 33d638, bulk-ahead-of-control 39e24f,
  no-credit 140f13, no-reset-on-end 86e82e, no-credit-on-refuse 002b2b, closed-ignored 0923b5,
  open-once 7f79c8, uncredited-kept fc8c65, main-feed dd075a.
- F4 at consensus:590308d (docs only): one bound, 164 + 52P, derived and beside the measured rows.
- F3 result at 48d04eb: reconnect 7b173e passes (1 unopened, 1 closed back); no-generation-clear
  889a1c killed (reconnect line 1504: delivered 2 of 2, 0 unopened); no-current 498ed0
  survives, as R7 predicts (equivalent).
- F5: `opening` returns before the erase when `!current` (48d04eb).
- F4: perf doc A1 rewritten from the A5 job at head (pending 261117-235521-9c2259).
- Reruns at 48d04eb: A1 scan -384fb5, main-feed -235757-353ffb; A2 -9b423b; A3 order -f1332a,
  identity -6e0053, TSan -fe8742, mutants -a4c728 -fa1840 -c5f364 -ba7ee1 -6bde5e -5cf7c7 -eb0aef;
  A5 -9c2259; suite 261117-235803-43b723.

## Doing (sessions 1-3)

Code at consensus:01e2c06 (main merged at d722e7b; credit wait, `uncredited`, bound `164 + 52P`).
Session 1's 12 exit-127 jobs quoted binary and argument as one word: superseded by these, submitted by
`.scratch/submit.sh` in the code worktree (one argv word each). Mutants regenerated at 01e2c06.
- A1: `scan` 261117-205609-750069; fails-without at main-feed 75d6da0 (`.scratch/mutants/main_feed.py HEAD edbb162`,
  now also adding `uncredited`) 261117-205619-b7decf.
- A3: `order` 261117-205614-ddf195; fails-without ring-at-front/order 261117-205624-60f65d,
  bulk-ahead-of-control/order 261117-205629-2d0bb7, no-credit/order 261117-205635-ba674d,
  bulk-ahead-of-control/upload 261117-205640-4bcb97, no-credit/credit 261117-205644-19bf1a,
  no-reset-on-end/refill 261117-205650-ca8493, closed-ignored/identity 261117-205655-0e4768,
  open-once/identity 261117-205701-055b4c, store-keeps-opens/identity 261117-205706-359ac3,
  uncredited-kept/scan 261117-205712-090779 (new); TSan, every groups case, 261117-205717-1de5e8.
- A2 strict `one 5 1 10000` x3: 261117-205722-8d9231. A5 `cost --quick`: 261117-205726-286944.
- A4 suite 261117-205742-5a313f. Hop smoke A/B vs edbb162 (strict, not a criterion): 261117-205819-0701eb.

## Session 3

- store-keeps-opens survived because no step re-stores the *receiver* while a peer's open holds:
  host 2's re-store under epoch2 came after its open was already refused (`opening` erased it).
  Added identity step "host 2 stores ok again under a third epoch": frames under host 1's epoch2
  open must stop arriving (0 of 20) and host 2 counts a mismatch.
- Killer committed consensus:39eeb37; mutant 81d9ec5 (store-keeps-opens at ed8f840): job 261117-224140-de7732;
  identity at head 261117-224124-dc2270. Local run at 39eeb37 passed.
- A6: CI logs (.scratch/ci/ in the code worktree): every failure is a near-total stall, not slow
  throughput: 0.0-18.0 MiB of ~128, 3-15 control frames of 55, longest gap 3.0-4.0 s, host stalls
  81-111 ms. In 44214025598 host 1's loop 1 took 5.8 s to drain after stop (about dead_after: a
  writer waiting on a full send buffer). Always alongside connection_test (ctest --parallel 2), but
  so is the passing 44227964457. Since 44089495483 (transport-groups' first CI).
- Local (Intel Mac, macOS 15.8.1; CI 15.7.9 VM): 32 of 32 at 4-way, 20 of 20 beside connection_test;
  under `taskpolicy -b` 3 of 24 fail (gaps 1.0-1.4 s, 20-48 MiB): degraded, not CI's stall.
  Samples (passing run): receiver socket holds 1.2-1.5 MiB unread with rcvwnd 0.3-16 KB, sender
  sndq 450 KB: a control frame waits behind about 4 MiB (kernel both ends plus a data frame), so its
  gap is that over the drain rate. Diag committed consensus:ed8f840 (printed on failure only),
  pushed for CI to show the stall.
- CI with the diag: 44254116276 (dispatch, ed8f840) all green; 44255750747 (dispatch) macos-x86_64
  green; push run 44253167825 cancelled by the dispatch. No failure to read yet (base rate on this
  branch before: 2 of 3 runs failed upload).
- CI correlation: in 44216299768 connection_test's coarse_writer took 9,414 ms beside upload (6.2 s
  here); but slow_reader runs 5.2-5.7 s on that runner in runs where upload passed, so not proof.
  Upload always starts 9.0-11.6 s into connection_test (ctest --parallel 2), passing runs too.
- b686ce2 adds links, host 1 sent/dropped to the samples (pushed). Probe branch
  probe/transport-feed-scan-upload (78dc721 = b686ce2 + 6 more `upload` ctest entries beside
  connection_test; never lands; .scratch/probe/probe_commit.py): run 44257918794.
- Paired probes, 7 uploads each on macos-x86_64 beside connection_test: with the sampler
  (44259140498) 7 of 7, without it (probe/transport-feed-scan-upload-nodiag at 39eeb37, 44258707594)
  7 of 7. Since ed8f840: 24 of 24 macos-x86_64 uploads passed (1 push, 2 dispatches, 3 probes). The
  failures cluster in time (2026-11-17 16:49Z to 11-18 05:41Z); the sampler does not change it (its
  syscalls are read-only: XNU tcp_usrreq.c TCP_CONNECTION_INFO, sys_socket.c FIONREAD).
- What the failing logs do show: the shared connection carries nearly nothing for 3-4 s from early
  in the run while host 2's loops stay responsive (they drain in 6-81 ms at stop), and once host 1's
  writer waited out dead_after (5.8 s drain). So either host 2's reader sat parked with bytes in its
  socket (a lost read wakeup: product, connection.h) or loopback TCP stopped delivering (the runner's
  kernel). The samples tell these apart (receiver unread vs sender sndq, windows, RTO, rexmit); no failure since to read.
- CI at b686ce2 (44257887335): linux-tsan connection_test read_slowly line 693
  (connection-slow-reader-flake's) and linux-x86_64-clang consensus_transport_peers
  (peers-retry-flake's); neither in this file set. macos-x86_64 green.
- Merged main (b7f48b2, tools/mutants.sh) at 13b32d3; pushed. All evidence resubmitted at 13b32d3.

## Next

1. On results: record numbers; any mutant that survives is a gap to close.
2. docs/perf/transport-idle-cost.md: the per-group table and bound from 261117-205726-286944, A2's job for
   10,000 groups, the credit wait; drop "at 10,000 groups connections end".
3. Evidence table; lead pushes for CI.

## Finding: A2's 781.7 B per group (session 2)

Not table growth. Diag jobs at side refs refs/diag/feed-scan-1..5 (consensus; never evidence):
1. 261117-200235-70c7af, 261117-200633-177b6a: the extra bytes are sending `Active` records with
   `outstanding > 0`, in no ring, nothing queued: 620-4,957 per lane on some hosts, none on others. Each
   holds its two emptied `std::deque`s' blocks (libc++ keeps one 4,096 B block and a 16 B map): the
   `10.47x16 10.47x4096` per 100 groups in A2's job. Every sender sent all 40,000 credit frames
   (queued 0); the stuck host had received 25,956-39,785 of them.
2. 261117-201457-eeabdc: at the stuck receiver the socket holds 2-100 KB unread (FIONREAD) and `in_`
   a partial frame; gone 0.8 s later: credit still in flight when the run ends, queued behind the
   burst. 261117-204455-6f15b3 sampled stuck readers at connection.h's `co_await task::yield()`
   (where 5), read as a parked reader: disproved by the lead's reader-yield-stall trace, job
   261117-212453-69d3b7 (12 runs, 10,000 groups): the longest yield was 26.6 ms wall and 12.7 ms
   CPU, one pass of the io_context FIFO behind 3 readers, none over 50 ms; the `where 5` readers
   were mid-drain when sampled. The quest was dropped. CI macos-x86_64 hit the same in-flight credit
   at 1,000 groups (3.00x4096 per 100 groups, 404.2 B, run 44230587749).
3. Clean runs: 298.2 B at 10,000 groups (stored 127.8, frames 170.5) against 283.6 B at 1,000
   (136.8, 146.8): a group costs about the same at both.
The bench read the bytes once sent and received held still for 200 ms; a credit frame arriving moves
neither, so it measured groups still in flight. Fix in the file set: `HostTransportStats::uncredited`
(a level), the bench waits for it to reach 0 (10 s at most, printed), `scan` asserts it.

## Design

**Cause.** `feed` picks each frame with `next()`, which walks `Lane::groups` (a `std::map` of
every group with something queued *or uncredited*) from a cursor; once control frames run out,
every pick walks all of them, and `erase_if` walks them again at the end of every feed. With
10,000 groups each holding uncredited data, a feed is O(groups) per frame and O(groups^2) per
burst, the loop stalls past `dead_after` (job 261117-092742-231632). `open_first`'s sorted
`opened` vector inserts are O(groups) too.

**One shape: `GroupTable<V>`.** Every per-group table in the transport is one container: a
dense `std::vector<{GroupId, V}>` (1.5x growth) plus a `u32` open-addressed index (linear
probing, splitmix64 then a division-free `(h * n) >> 64` range map, load 0.5-0.75,
backward-shift deletion, swap-remove of the dense slot). A set is `GroupTable<Empty>`
(`[[no_unique_address]]`: 8 B an entry). It replaces the `std::unordered_map`, the `std::map`s,
the `std::set` and the sorted vector.

**Ownership per loop.** Nothing is shared between loops except what a post carries by value.
- Group loop (the group's): `stored: GroupTable<Stored>`, `Stored = {incarnation, serial, GroupOpen}`
  (96 B, the open by value: no `shared_ptr<Opening>` block); `serial` from a per-loop counter
  (a group never changes loop, so it rises per group; the transport-wide atomic goes).
  `opened: map<(node, lane), {generation, GroupTable<Empty>}>`: the groups whose peer open
  matched on that connection. A batch of a newer generation clears its set (the connection
  changed), so entries carry no generation; batches of one connection reach a group loop in
  generation order (one connection loop posts them in order). `store`/`forget` erase the group
  from each set (O(2P)).
- Connection loop, per lane: `opened: GroupTable<u64>` (group -> serial of the open the
  connection carries) and `active: GroupTable<Active>`, one record per group with anything in
  flight: control and data queues, queued/outstanding/owed bytes, a `closed` to tell, the
  latest frame's open by value, and the ring links. `grants` (map) and `closes` (set) fold into
  `Active::owed` and `Active::close`. An idle group has no `Active`: it costs only its
  `opened` entry.
- `Outgoing` carries `{GroupOpen, serial}` by value (88 B, a copy instead of a refcount).

**Ready lists.** Three FIFO rings per lane, intrusive through `Active` by group id (head, tail,
size in the lane; `next[3]` and an `in` bitmask in the record; no allocation): `owed` (credit,
then closed), `control`, `data`. A group is in `control` iff it has a control frame; in `data`
iff its front data frame fits its credit (`!outstanding || outstanding + size <= window`). Pushes
happen where readiness changes: enqueue, credit arriving, a grant or refusal posted back,
connection established or ended (outstanding resets: the rings are rebuilt from `active`, O(active),
once per connection event), and after a pop when the group is still ready. `feed` pops one group
per frame: O(1) per frame, nothing scanned. An `Active` with nothing left and in no ring is
erased where it empties.

**Ordering and fairness (A3).** Each feed picks owed, then control, then data, as before, so a
control frame waits behind at most one data frame. Round robin: a popped group that is still
ready goes to the back of its ring, so N ready groups alternate one frame each (first-ready
order rather than id order: still round robin). Credit is per group: a group blocked on credit is
in no `data` ring and costs nothing until its credit arrives; other groups' frames go meanwhile.

**Counting probe (A1).** `HostTransportStats::visited`: groups the feed examined to choose a
frame (one per ring pop). `scan` sends a control and a data frame per group for 100, 1,000 and
10,000 groups and asserts visited per frame sent stays under 2; at main's code with the counter
in `next()` and `erase_if` it grows with the group count.

**A5, field by field (within the store/open contract).** What an idle group must hold: on its
loop, the id (8), incarnation (8), serial (8) and its open (80: cluster, group, epoch 16 each,
profile 32; the profile hashes the group's configuration and voters, so it is per group and is
not interned); per connection that opened it, its id in that connection's set (8) on the group
loop; per connection that carries its open, id and serial (16) on the connection's loop; plus the
tables' index and growth slack. 104 B + 24 B per peer before slack, against 206 B + about 40 B
now. 64 B needs the open out of the transport (an `open_of(group)` callback like `member_of`:
the host already holds it; the record becomes 24 B) or a digest open frame (a wire change):
both change a contract, so they are the lead's (see Blocked on).

## Blocked on

- Nothing. A6 was rescoped by the lead to the failure samples (an [artifact]); the cause goes
  to quest groups-upload-stall.
- (lead, FYI) F1's control stream stays 20-30% over main (strict 757230: main 513/331 ns, branch 605/429):
  two GroupTable lookups per frame on the group loop vs main's one map find. Not in F1's ask
  (data per frame); options: accept and record, or a follow-up that folds the opened check into
  `Stored` (costs idle bytes) or speeds the lookups. Recommend accept.

## Findings (review fix round 2)

- G1 = 261118-034609-0c0786 (coalesce-drops-credit at 91da6ea fails `backlog`; reached and
  killed by the review script 261118-034611-6144e1); suite 261118-034613-a94a25. Pending.

## Findings (review fix round 1)

- F1 = 261118-002026-757230 (strict A/B at 303cad1: data at or under main at each position);
  deciding exclusive 261118-003330-778798 pending. Suite 261118-045252-a6a671.
- F2 = 261118-003144-3730d0 (store-keeps-opens killed by identity at 303cad1; also
  261117-224140-de7732 at 13b32d3, aborting on line 1234's epoch3 assertion).
- F3 = 261118-003140-29e975 (no-generation-clear killed by `reconnect` at 303cad1; plain
  261118-045306-30cac2). no-current survives (261117-235613-498ed0): equivalent today (R7).
- F4 = consensus:590308d, docs only, rows from 261118-045305-69c770.
- F5 (non-blocking) fixed in 48d04eb; no test can fail without it for the same reason as
  no-current (R7).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-045306-c3b884 (afaa58e): `scan` 0.750 visited per frame at 100, 1,000 and 10,000 groups; fails-without main-feed 261118-003230-dd075a: 28.5, 255.2, 2,529.1 (asserts); uncredited-kept 261118-003225-fc8c65 |
| A2 | 261118-045307-857f92 (strict, afaa58e): 3 of 3, 400,040 of 400,040 frames, 298.2 B a group (stored 127.8, frames 170.5), credit back 561-683 ms after |
| A3 | order 261118-045306-b6154a, identity -045305-fb5ea5, reconnect -045306-30cac2, leftover -045306-6e79b8, TSan -045305-8c4955 (all afaa58e); fails-without at 303cad1: ring-at-front 261118-003149-33d638, bulk-ahead-of-control -003154-39e24f, no-credit -003159-140f13, no-reset-on-end -003205-86e82e, no-credit-on-refuse -003209-002b2b, closed-ignored -003215-0923b5, open-once -003219-7f79c8, store-keeps-opens -003144-3730d0, no-generation-clear -003140-29e975 |
| A4 | suite 261118-045252-a6a671 (afaa58e); CI at afaa58e |
| A5 | 261118-045305-69c770 (afaa58e): 172.1-295.2 B a group, every row under 164 + 52P; field-by-field minimum 104 + 24P (Design, perf doc A1) |
| A6 | `test/transport/groups_test.cc`: "every 250 ms: progress, links, host 1's frames, host 2's reads, the sockets" |

## Result

<pending>

## Recommendation (follow-up, not this quest)

The per-group queues as a vector FIFO (compacted when half consumed) instead of `std::deque`: a group
in flight then holds tens of bytes instead of 8.2 KB (R6).

An `open_of(group)` callback, as `member_of` is, so the transport stops holding a copy of the
open the host already has: an idle group's record goes from 104 B to 24 B, which brings it
under D108's 64 B. It changes the `store` contract and Detent's call sites. The lead deferred it.

## Rulings I made

- R7 (F3) no-current is an equivalent mutant today: every connection of a (node, lane) lives on
  one loop, whose batches post in order, and a host's connection drains only at the peer's
  `close_notify`, after which it reads nothing (HostTransport never calls `Links::retire`). So no
  older-generation batch reaches a group loop after a newer one; `current` stays as the guard.
  F9's "a draining older connection can still read" does not hold for this transport; the
  mechanism doc says why.

- R1 Within-contract A5: the field-by-field route the quest allows; the open stays in the transport.
- R2 Round robin in first-ready order (FIFO rings) instead of group-id order.
- R3 `opened` on the group loop is per connection, cleared by a newer generation, instead of a
  `(node, lane, generation)` vector per group.
- R4 The bench waits for `uncredited == 0` before reading idle bytes, which is what its "credit
  returns" comment meant; it counts a 10 s miss as a failure, not a retry.
- R5 Per-group bound `164 + 52P`: A5's minimum (104 B stored, 8 + 16 B per peer) at the tables' worst
  slack, 1.5x an entry for the dense vector plus 8 B for the index at load 0.5. Replaces `230 + 40P`.
- R6 Not changed here: an `Active` waiting only for credit holds 2 x (4,096 + 16) B of emptied deques
  (main's `Lane::Group` did too). At 10,000 groups with a frame in flight per peer that is about
  330 MB a host. A small vector FIFO would make it about 64 B: a follow-up (Recommendation).
