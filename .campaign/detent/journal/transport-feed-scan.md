# transport-feed-scan: journal

State: working
Quest: .campaign/detent/quests/transport-feed-scan.md (the lead's; never edit it)

## Doing

Implementing the design below in consensus `include/consensus/transport/host_transport.h`.

## Next

1. Code: `GroupTable`, rings, compact `Stored`, per-connection opened sets.
2. groups_test cases `scan` (A1 probe) and `order` (A3), run by name in jobs until CMakeLists.txt is in the file set (msg-66 to the lead).
3. Jobs: suite, A1 probe + `--fails-without` at main's feed with the counter, A3 mutants, TSan groups, A2 `one 5 1 10000`, A5 idle cost at 1,000 groups, hop smoke A/B.

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

- msg-66 (to the lead): add `CMakeLists.txt` and `test/transport/mutant_commit.py` to the file
  set. Until then new cases run by name in jobs and mutants come from a scratch script.
- Not blocking: A5's 64 B needs a contract change, options above (recommend `open_of`).

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

## Rulings I made

- R1 Within-contract A5: the field-by-field route the quest allows; the open stays in the transport.
- R2 Round robin in first-ready order (FIFO rings) instead of group-id order.
- R3 `opened` on the group loop is per connection, cleared by a newer generation, instead of a
  `(node, lane, generation)` vector per group.
