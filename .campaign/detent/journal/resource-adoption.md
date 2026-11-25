# resource-adoption: journal

State: waiting on jobs 261125-051931-4c08af
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

Local iteration build (RelWithDebInfo, Apple clang, authority+relay+agent, allocators override at
allocators:73baa46): 217 of 218 tests pass, `test_local_trusted_paths` skips (privileged, as on
main) and `test_reader_switch` does not build: it includes `src/detent_local/lazy_page_access.hh`
without linking the target that carries allocators' include path (Blocked on 1). With the one-line
fix applied locally and reverted, it builds and passes (216 s). Not evidence: local, not a job.

Not pushed: Detent CI would fail on `test_reader_switch` until Blocked on 1 is ruled. The allocators
quest branch is pushed; its CI run 45622927366 was still queued at 05:55.

## Next

1. When 261125-051931-4c08af ends: Decision 3. Taken: cherry-pick allocators:1567b18 onto the
   allocators quest branch; otherwise the head stays allocators:73baa46. Rerun allocators CI
   (run 45622927366: all green but macos-arm64, cancelled after 33 min with no runner), push main.
2. Pin cascade once, at that head (lead ruling 2): completion (allocators) -> faultlab (completion)
   -> journal (completion, allocators, faultlab) -> consensus (completion, journal, faultlab), one
   commit each in `~/code/Kronuz/<lib>-wt/detent/resource-adoption` (branches made at origin/main);
   push the quest branches together, each main once its CI is green; Detent pins all five.
3. At the final Detent head: `campaign verify` (native, pins, asan, tsan); A1 grep; A5 and A12
   (`benchmarks/resource_qualification.sh bytes|targets`, light); A7 `first-read` and A13/A14
   `costs` (exclusive); A6 mutant (Detent:3ed99815 rebuilt on the final head) and retirement tests;
   A9 fails-without: allocators' `test/retirement.cc` at the old two-load step (local run: close
   and rollback at `worker_admitted` lose the wakeup, 2 of 4). Push Detent; ready for review.

## Blocked on

Nothing: the lead widened the set to `tests/detent_reader/CMakeLists.txt` (Detent:cc061930) and
ruled the cascade (msg from the lead, 2026-11-25: once, after the A/B; consensus main is right).

## Evidence

| Criterion | Evidence |
|---|---|
| A5 (dry run, not evidence) | local `resource_qualification.sh bytes`: 7 wire captures, both stores (9 and 6,003 files) and 8 authority byte counts identical to main and repeatable on main |
| A12 (dry run, not evidence) | local `targets`: default and all-components configures fetch the same set as main; tracked memory off; only allocators' `retirement.cc` compiles; no legacy library |

## Result

## Rulings I made

0. Worktree: added by hand at `../Detent-wt/detent/resource-adoption` because state.json had no
   branch and `campaign start` refuses an active quest; the lead accepted it and reports the tool
   gap. A job submitted without `--commit` ran at the home's main (261125-051912-c12324, cancelled).

1. Merge strategy: main's code wins every conflict; the 2026-11-02 adoption is redone on main's
   code, not replayed (D162 "restart from current main").
2. A9: main deleted the paged segment collector and `test_paged_segment_collection.cc`
   (Detent:44e8d0c9, "Wire DiskStore through the agent store, delete the old layout"), so variant
   13's test and the fix Detent:febdef6 have no file to apply to. The tests left on main that admit
   `limit - used` hold the ticket only across one expected refusal or one operation that charges
   nothing (`test_paged_disk_store.cc` scenarios 7 and the recovery confirm), never across a retry
   that needs freed headroom, so the race's shape does not recur.
