# dense-sync-stack: journal

State: waiting on jobs 261118-055412-608f32 261118-055412-a8791e 261118-055412-1bba5b 261118-054101-b5ef57 261118-055013-136e37 261118-055014-19aed1 261118-055016-95665a 261118-054306-b5fe77 261118-054307-5a3cb1 261118-054308-15108e 261118-054309-71faa7 261118-054310-9848a1 261118-054311-2e72e9 261118-054312-c42fcf 261118-054313-8e49b7 261118-054314-792ca7 261118-054315-78f04e 261118-054316-9f93de 261118-054317-59781b
Quest: .campaign/detent/quests/dense-sync-stack.md (the lead's; never edit it)

## Doing

Answer: yes, DTPAGE03 publications already sync in D11's grouped order on main. The dense-pages
merge of S1/S2 (Detent:8eeb36e, landed Detent:d04794d) put paged artifacts in the two groups, and
namespace-s7/s8 (Detent:44e8d0c) now route every DiskStore publication, flat or paged, through
`AgentStore::publish`: owner records + lease group, payload group (pack, image, read, live), table
synced alone, rename, directory sync, `change` (src/detent_local/agent_store.cc). Nothing to wire.
What was missing was proof: no test checked a Dense publication's counters against the seam, and
T7 (tests/detent_agent/test_agent_store_crash.cc) publishes synthetic bytes, not DTPAGE03.

Added (Detent:2957619, Detent:5377a48): `detail::DiskStoreTesting` (store_core.hh, disk_store.cc), a
hook on each store's PosixIO; tests/detent_local/test_paged_publication_artifacts.cc now records a
DiskStore delta and progress publication plus collection on the sync seam, checks startup and
publication counters against it (groups == 2), cuts (kill; power cut Durable/All x Truncate/Zeros)
after every event, reopens each cut (Ready, selects base or published, published once served, reads
the exact state commitment), republishes on the base, and cuts each restart's republication at every
step; then checks the grouped order.

## Next

1. Read 136e37 (A1-A3 test) and the two mutants (19aed1 n6, 95665a owner group unsynced): each must
   print "reached the test" and fail. If the owner-group mutant survives, recovery does not read
   `.owner` bytes: report it.
2. Read A1 cold boot b5ef57 (100K at main: "in N groups", N > 0; also says whether the Oct-23
   100000-r3 fixture still loads on main). Rerun it at the final head.
3. Read the 12 A4 arms (100K, exclusive, one arm-run per job; arms dense=Detent:9852a3c,
   s2=Detent:35296ba, stacked=Detent:d04794d (first tree with both), main=Detent:ea1fddc; 3 rounds
   each, interleaved). Write docs/perf/dense-sync-stack.md (A6).
4. Gates queued: native 608f32, asan a8791e, tsan 1bba5b; branch pushed (CI on quest/detent/dense-sync-stack).

## Blocked on

Lead: A4 at 5M. `.scratch/capacity-trials/5000000-flat-r1` (the 5M fixture of 11-07 and A9) is
gone; only 10000-r3, 100000-r3 and 130000-r3 remain. And a 5M arm (about 8-10 min boot plus
restarts) exceeds the 10-minute exclusive cap, so it needs `--long` (deferred, proves nothing
before landing). Options: (a) regenerate the 5M fixture with run_fleet (5000000.json) at a tree all
four arms read, as a light `--long` job, then one `--long` exclusive job per arm; (b) accept 100K
plus 130K rows and the 11-07 5M screen as the 5M context; (c) drop 5M. Recommend (b) now, (a) as a
milestone check if the 5M number must decide something.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-055013-136e37 (grouped order in the DTPAGE03 test); 261118-054101-b5ef57 (100K log line) |
| A2 | 261118-055013-136e37; mutants 261118-055014-19aed1, 261118-055016-95665a (--fails-without) |
| A3 | 261118-055013-136e37 |
| A4 | pending: 12 exclusive 100K jobs above |
| A5 | CI on push |
| A6 | docs/perf/dense-sync-stack.md (pending) |

## Result

Pending.

## Rulings I made

1. The quest's file set predates the agent store: storage_session.cc is gone and
   tests/detent_local/test_publication_power_cut.cc is unregistered (registering it needs
   tests/detent_local/CMakeLists.txt, held by namespace-cleanup). The DTPAGE03 T7 and counter
   checks live in test_paged_publication_artifacts.cc, which is registered and in the set.
2. The power-cut model (Disk, seam recording, after_power_cut) repeats ~120 lines of
   test_agent_store_crash.cc, which is outside the set. Recommend a shared header in
   tests/detent_agent once a quest holds both files.
3. The test plans each generation on the open store's `ManagedRecovery::recover`, as the agent and
   generation_writer do, so restarts over cut leftovers plan from what the store recovered.
4. A4 arms: S2 alone is Detent:35296ba (A9's landed S1/S2 tree, no Dense); Dense alone is
   Detent:9852a3c (the 11-07 screen's tree); stacked adds Detent:d04794d, the first tree with both,
   beside main, which also carries the agent store and later work.
