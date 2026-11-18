# transport-feed-scan: journal

State: waiting on jobs 261117-205609-750069 261117-205614-ddf195 261117-205619-b7decf 261117-205624-60f65d
261117-205629-2d0bb7 261117-205635-ba674d 261117-205640-4bcb97 261117-205644-19bf1a 261117-205650-ca8493
261117-205655-0e4768 261117-205701-055b4c 261117-205706-359ac3 261117-205712-090779 261117-205717-1de5e8
261117-205722-8d9231 261117-205726-286944 261117-205742-5a313f 261117-205819-0701eb; CI needs a push of
consensus:quest/detent/transport-feed-scan at 01e2c06 (contributors do not push).
Quest: .campaign/detent/quests/transport-feed-scan.md (the lead's; never edit it)

## Doing

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
   a partial frame; gone 0.8 s later. 261117-204455-6f15b3 (12 runs, 4 stuck): every stuck reader is
   parked at connection.h's `co_await task::yield()` after a successful 1,281-5,061 B `SSL_read`
   (where 5); idle readers wait in `want(SSL_ERROR_WANT_READ)`. The yield's post is not resumed for
   0.4-1.2 s while the loop runs other handlers (the diag's own `at()` ran). Load-dependent: 0 of 8
   runs stuck at load 5-7, 4 of 12 at load 8-15. CI macos-x86_64 hit it at 1,000 groups (3.00x4096
   per 100 groups, 404.2 B, run 44230587749).
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

- (lead) The reader stall above is in connection.h / task scheduling, outside this quest's file set
  and unchanged by it (`git diff main -- include/consensus/transport/connection.h` is empty). A yielded
  reader not resumed for up to 1.2 s is a latency fault for every connection, not only this bench.
  Options: (a) a quest for it (recommended; the diag refs and jobs above reproduce it under load,
  about 1 run in 3 at 10,000 groups); (b) fold it into connection-slow-reader-flake. Not
  attributed at main by a run: main's feed stalls at 10,000 groups for its own reason.
- (lead) CI: push consensus:quest/detent/transport-feed-scan (01e2c06 or later).

- msg-66 (to the lead): granted and closed; `scan` and `order` are in the CMakeLists foreach
  (only that line changed) and the mutants are in mutant_commit.py.
- Nothing. A5 takes the field-by-field route (lead: 104 B + 24 B per peer meets it).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

<pending>

## Recommendation (follow-up, not this quest)

The per-group queues as a vector FIFO (compacted when half consumed) instead of `std::deque`: a group
in flight then holds tens of bytes instead of 8.2 KB (R6).

An `open_of(group)` callback, as `member_of` is, so the transport stops holding a copy of the
open the host already has: an idle group's record goes from 104 B to 24 B, which brings it
under D108's 64 B. It changes the `store` contract and Detent's call sites. The lead deferred it.

## Rulings I made

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
