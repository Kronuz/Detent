# resource-adoption: journal

State: working
Quest: .campaign/detent/quests/resource-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Restart per D162. Worktree `../Detent-wt/detent/resource-adoption` on `quest/detent/resource-adoption`;
state.json records no branch or worktree for this quest (its status is active, so `campaign start`
refuses), so I added the worktree at the path `start` uses; jobs pass `--commit` explicitly. The old
`../Detent-wt/resource-adoption` (on `phase/resource-adoption`, the same old commit) is left alone.

Done: merge of main (Detent:8918a935, main's code wins); allocators quest branch
(allocators:6850f04 fix + absorb + recount, allocators:73baa46 benches; pushed, CI 45622927366);
Detent adoption (Detent:e4575f9a), qualification script and pin at allocators:73baa46
(Detent:090679a0). Decision 3 candidate: allocators:1567b18 on `explore/detent/resource-adoption-outline`
(local), A/B job 261125-051931-4c08af queued. A6 mutant commit Detent:3ed99815
(`mutant/detent/resource-adoption-settle`, local).

Local iteration build (allocators override): everything compiles except `test_reader_switch`, which
includes `src/detent_local/lazy_page_access.hh` without linking the target that carries its include
paths (see Blocked on). Suite run in progress.

## Next

1. Decision 3 from the A/B; then the allocators head is final: CI green, push allocators main.
2. Pin cascade (see Blocked on): completion, journal, faultlab and consensus declare allocators or
   completion; A2 needs them at the new heads.
3. Jobs at the final head: native, pins, asan, tsan (verify); A1 grep; A5/A12 light; A7/A13/A14
   exclusive; A6 mutant and retirement tests.

## Blocked on

1. File outside the set: `tests/detent_reader/CMakeLists.txt`. `test_reader_switch` includes
   `src/detent_local/lazy_page_access.hh`, which now reaches `<allocators/storage.h>`, but links only
   `detent_reader_hooked` (local_view private) and `detent::testing_store`. Fix: add
   `detent::resources` to its `target_link_libraries` (one line). Recommendation: add the file to the
   set; the alternative, making `detent_reader_hooked` link `detent::local_view` PUBLIC
   (`src/detent_reader/CMakeLists.txt`), is also outside the set and widens a test-only target.
2. Pin cascade (A2): `completion` and `journal` declare allocators ae775ab; `journal`, `faultlab` and
   `consensus` declare completion; `consensus` declares journal. Pinning a new allocators needs
   bump commits on completion, journal, faultlab and consensus mains, and consensus main
   (217481b) is 10 commits past Detent's pin (72cc58d) with other quests' work. Options: (a) I push the
   four bumps (owner's cpp-library push authority) and Detent pins all five heads, taking consensus
   head with it; (b) the lead folds it into a pin wave. Recommendation: (a) for completion, journal and
   faultlab, whose heads equal Detent's pins; the lead rules on consensus.

## Evidence

| Criterion | Evidence |
|---|---|

## Result

## Rulings I made

1. Merge strategy: main's code wins every conflict; the 2026-11-02 adoption is redone on main's
   code, not replayed (D162 "restart from current main").
2. A9: main deleted the paged segment collector and `test_paged_segment_collection.cc`
   (Detent:44e8d0c9, "Wire DiskStore through the agent store, delete the old layout"), so variant
   13's test and the fix Detent:febdef6 have no file to apply to. The tests left on main that admit
   `limit - used` hold the ticket only across one expected refusal or one operation that charges
   nothing (`test_paged_disk_store.cc` scenarios 7 and the recovery confirm), never across a retry
   that needs freed headroom, so the race's shape does not recur.
