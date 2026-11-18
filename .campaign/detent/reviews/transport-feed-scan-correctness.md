# transport-feed-scan review: correctness

Commit: consensus:5eacc91e165d707cc4e9521ed9f6ebdfb50b1d5d
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The ready rings and `GroupTable` are correct under every probe I ran. Four things block. The
receiver's per-frame CPU on a data stream is 2.4-2.8x main's, which the contributor's own smoke
job shows and nobody reported. Two of the identity rules this quest rewrote have no test that
fails without them. The perf doc contradicts itself on the bound.

## Evidence I relied on

- A1 `scan` 261117-205609-750069 (0.750 visited per frame at 100, 1,000 and 10,000 groups) and its
  fails-without at main's feed 261117-205619-b7decf (28.5, 255.2, 2,529.1). Every frame a feed
  sends comes from `pop` (host_transport.h:453), which is the only place `visited` is counted. The
  owed, control and data branches of `feed` all pop, so the count covers every feed path. The
  count is O(1) by construction, so the timing proof that the scan is gone is A2.
- A2 261117-205722-8d9231: 3 of 3 at 10,000 groups, no connection ended, every frame delivered.
- A3 `order` 261117-205614-ddf195 and the mutants ring-at-front, bulk-ahead-of-control,
  no-credit, no-reset-on-end, closed-ignored, open-once and uncredited-kept. Each one fails a
  test, as recorded in the journal's job list.
- TSan 261117-205717-1de5e8, every groups case. Suite 261117-205742-5a313f at 01e2c06; the only
  code between 01e2c06 and 5eacc91 is docs.
- A5 `cost --quick` 261117-205726-286944. A group costs 173.4, 210.2 and 283.6 B at L = 1 and
  P = 1, 2, 4, and 172.1, 213.1 and 295.2 B at L = 4. "Stored" is 130.6-136.8 B and "its frames"
  is about 37-41 B per peer, every point under `164 + 52P`.
- The contributor's hop smoke A/B 261117-205819-0701eb, which I read for absolute numbers (F1).

## What I checked myself

Scripts are in `.scratch/review-transport-feed-scan-correctness/` in the Detent home. Each job ran
at consensus:5eacc91 at the light level.

- `run.sh` (`table.cc`), job 261117-222227-5ebe28, built with ASan and UBSan. It compares
  `GroupTable` with `std::unordered_map` over 2,560,000 random insert, erase, find and contains
  operations, with 25,508 full comparisons. Pools run from 4 to 5,000 ids, including ids near 0
  and 2^64, so collisions, wraparound, growth, shrink and clear-on-empty all occur. It then
  inserts 100,000 sequential ids and drains them. Result: ok. The `fs_rings` step after it ran
  past the 30-minute deadline, so it was rerun smaller.
- `run-rings.sh` (`rings.cc`, `growth.cc`), job 261117-231112-e0fbc4, ASan and UBSan. The ready
  rings run white-box against a FIFO model for 64,000 steps (15,922 pops) of push, pop, re-push
  and settle. Other groups are created and erased meanwhile, so swap-remove and growth move every
  `Active`. Each step checks order, size, tail, links and the `in` bits. Result: ok. `growth.cc`
  covers F6.
- `ab.sh`, job 261117-222407-e9a174, run base, branch, variant, variant, branch, base. Base is
  main at consensus:9f86ec3. Variant is the branch with `GroupTable::erase`'s clear-on-empty and
  shrink disabled. The bench is `hop 3 0` and `hop 3 1`. This is F1.
- `profile.sh`, job 261117-222448-98fffc: macOS `sample` of `hop 6 1` for main and the branch.
  This is F1.
- `mutants.sh`, job 261117-222548-be71b1: two mutants of the generation rule, each run through
  all 13 groups cases. This is F3.

Answers to the lead's questions:

1. The rings neither lose, double-queue nor starve a group, by reading and by the ring model. A
   group's ring membership is a bit in `in`, so a second push is a no-op. Readiness changes only
   at the places that push: enqueue, credit, grant, a pop that leaves the group ready, and
   `reset`. Owed comes first, then control, then data, so a control frame waits behind at most
   one data frame. Credit is per group, and round robin follows the order groups became ready.
   Control has strict priority over data, as on main.
2. Rings link by group id, never by slot, so swap-remove and reallocation cannot leave them
   stale. An `Active` is erased only when `in == 0`, so a ring never names an erased group. No
   reference to a table entry is held across an insert into the same table (enqueue, grant,
   feed, open_first, deliver, opening). The table itself matches the oracle. The generation
   cases are not covered: F3.
3. In the code, an old open cannot match a new incarnation. `store` and `forget` clear the group
   from every connection set on its loop, and a newer generation's batch clears the set while an
   older generation is never current. The serial is per loop, which is sound because a group
   never changes loop. But neither rule is tested (F2, F3), and one old-generation path is wrong
   (F5).
4. The `uncredited` wait excludes only credit in flight. A credit that never returns fails the
   run after 10 s, so it is not hidden. The transient cost of a group whose credit is in flight,
   2 x 4,112 B of emptied deques, is real, and the doc states it (R6). `164 + 52P` is a ceiling
   from the tables' worst slack, not a fit. The bytes sit 27-33 B under it for "stored" and
   11-15 B per peer under it for "its frames". The doc does not state this consistently: F4.
5. Yes: see A1 above.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The receiver's CPU per data frame roughly 2.5x main's in the hop stream (256 groups, 60 frames each per round). The contributor's strict A/B measured 644 and 647 ns per frame at main against 1,575 and 1,608 on the branch; hop was 834-843 against 1,655-1,703. Control rose 10-20%. Its verdict line judges only hop minus same, which is why "passed" hid this. My light rerun agrees: base 1,092 and 794 ns, branch 3,106 and 1,876, variant 1,908 and 1,387. The profile puts it on the receiver. Per batch, the credit `GroupTable` is built from empty, and each credited group gets a 248 B `Active` whose `lane.active[group]` insert later goes through `settle`'s erase. The table grows, shrinks and frees every batch. Branch samples: `GroupTable<Active>::operator[]` 479, `GroupTable<u64>::operator[]` 458, `erase` 402, `settle` 238, `madvise` 783 (129 at main). Disabling shrink and clear recovers about a third. The sender's `next()` scan, which main spends 2,842 samples on, is gone. The quest trades a sender walk for receiver churn and reports neither. | 261117-205819-0701eb, 261117-222407-e9a174, 261117-222448-98fffc; host_transport.h:134-156 (erase, shrink, clear), :713 (per-batch credit table), :757-765 (Active per credited group) | Keep the receiver's credit and owed path off the per-batch alloc/free cycle. Options: keep `deliver`'s credit table as a per-loop member that is emptied, not freed. Hold owed credit and closes in a light owed table instead of a full `Active`. Give `GroupTable` hysteresis, so it does not free at empty and does not shrink below a floor. Then rerun the hop A/B (`hop 3 0` and `hop 3 1`, base main) with a rule fixed first on the receiver's absolute ns per frame, and show the branch within main's A/A band. Failing that, record the cost with its numbers in a lead Decisions row. |
| F2 | yes | gap | The store-keeps-opens mutant survives (job failed: the test passed without the fix). Nothing tests that a receiver which stores a group again closes the group on every connection. With the mutant, a sender still holding the previous open has its frames admitted after the receiver re-stores the group under another epoch. That is the open check transport-identity's S1 fix protects. `identity` only stores again on the sender first, whose mismatched open erases the entry before the mutant can matter. The contributor is on it. | 261117-205706-359ac3; host_transport.h:284 (`unopen` in `store`) | An `identity` step where host 2 stores `ok` under another epoch while host 1 keeps the old one. Host 1's frames count as `unopened`, and none are delivered until both agree. A fails-without job on store-keeps-opens. |
| F3 | yes | gap | No test exercises the per-connection generation rule, which replaced the exact (node, lane, generation) match. Two mutants pass all 13 groups cases. no-generation-clear means a newer generation keeps the older connection's opened set. no-current means a late older-generation batch counts as current. Honest senders reopen on every new connection, so only a peer that sends a frame without the open on a new connection would get through. That case is what the rule exists for. | 261117-222548-be71b1; host_transport.h:707-712, :724 | A raw-peer case, as `leftover`'s attacker does. Open g on connection 1 and end it. On connection 2, send g's frame without its open: it must be refused as `unopened`. Add both mutants to mutant_commit.py, each with a fails-without job. |
| F4 | yes | gap | At this commit the perf doc says the CI bound is `164 + 52P` "(A1 below)". Its A1 section still gives the old 76bacd2 table, the old composition (`Stored::opened`, `make_shared` Opening) and "The bound is `230 + 40P` (D112)". A reader cannot tell which bound holds, nor where 164 and 52 come from. The journal lists this as Next 2. | docs/perf/transport-idle-cost.md:17, :77-104 at 5eacc91 | Replace A1's table with 261117-205726-286944's rows. State the field-by-field minimum, 104 + 24P, and the worst-slack ceiling, 1.5x the entry plus 8 B of index per table, which gives `164 + 52P`. Give the measured fit beside it, about 137 + 37P at L = 1. |
| F5 | no | defect | `opening` erases the group from the current connection's set before it checks `current`. A late open arriving on an older connection of the same lane therefore closes the group on the newer connection, which has already opened it. The group's next frames there are refused, and a `closed` round trip follows before the sender reopens. This needs two live connections of one lane, a draining one and its replacement. | host_transport.h:783-784 | Return false before the erase when `!current`. F3's case can cover it. |
| F6 | no | defect | Under libstdc++, `std::deque`'s move constructor is not noexcept. `GroupTable<Active>`'s vector therefore copies every `Active` on growth, queued frames included: 1,934 frame copies for 1,000 groups with one frame each, against 0 under libc++. Linux, the gate platform (D111), uses libstdc++. Correctness holds. The cost is O(bytes queued) per growth. | 261117-231112-e0fbc4 (`growth.cc`, g++-16 and Apple clang) | Hold the queues in a type with a noexcept move, such as R6's vector FIFO. Or static_assert `std::is_nothrow_move_constructible_v<Entry>` in `GroupTable`. |
| F7 | no | nit | `164 + 52P` is per peer with group frames on one lane, since the bench sends only on Control. A group opened on both Control and Bulk holds 24 B more per peer for the second lane. The doc's field list says "per connection", but the bound says P. | bench/transport_idle.cc:550 | Say "per peer, one lane" beside the bound, or count connections. |
| F8 | no | nit | A sender's `Lane::opened` keeps a forgotten group's entry (16 B) until the connection resets or a `closed` arrives. Main's sorted vector did the same. | host_transport.h:391, :637 | Leave it, or drop entries on a `closed` for an unstored group. |
| F9 | no | nit | host-transport.md justifies clearing on a newer generation with "batches of one connection reach a group loop in generation order". Batches of one connection do arrive in order, but a draining older connection of the lane can still read after its replacement's batches arrive. The code handles that with `current`. | docs/mechanisms/host-transport.md:72-74 | Say that older generations' batches count as stale. |

## Not run

- CI at 5eacc91: macos-x86_64 fails `groups_upload`, which is A6 and the contributor's. Every
  other platform passed, run 44245984014.
- No strict or exclusive measurement. F1's numbers rest on the contributor's strict job, and my
  light A/B and profile only attribute them. F1's fix needs its own strict row.
- I did not rerun A2, A5, TSan or the suite, and did not review A6 or the mutant work the
  contributor has in progress.
- Debug-build asserts in the transport, beyond my probes' own asserts.
