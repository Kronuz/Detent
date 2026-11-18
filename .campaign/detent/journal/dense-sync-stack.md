# dense-sync-stack: journal

State: waiting on jobs 261118-064312-81a63c 261118-064403-1e5071 261118-064414-eebae8 261118-064419-b6ee71 261118-064421-90a1aa 261118-064422-da3323 261118-064424-b03bdd 261118-064425-54b693 261118-064427-8657da 261118-064436-028406 261118-064438-51839b 261118-064115-2e8717 (plus the 100K and gate jobs below)
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

## 5M (D119)

Fixture: `/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials/5000000-flat-r1` (home .scratch, outside any job checkout; kept, never rebuilt: the seed job exits at once if `seeded.json` is there). Seeded as run_fleet.py did on 2026-10-24 (scenario `.scratch/capacity-trials/5000000.json`, preset capacity-5000000.json, flat writers, seed 17): `docs/measurements/dense-sync-stack-20261118/seed.py` stops after prepare+verify. Arms only `cp -cR` its state-0..2 (coldboot.py), never write it. A failed seed renames it to `.partial`.

1. Seed job 261118-064312-81a63c: light, 135 min (`--long`, deferred: 2026-10-24 seed took 5,484 s), at Detent:9852a3c (oldest arm tree, so the most trees can read it; the harness API of Authority and Writers is the same on all four), `--env FIXTURE=5000000-flat-r1`. On success fill path, size (`fixture-size` in the job out), seed commit and job id into docs/measurements/dense-sync-stack-20261118/README.md and Result below.
2. Arms, `--after` the seed job, exclusive, `--minutes 28 --long` (the campaign caps exclusive at 10 min; a 5M arm is 8-18 min plus a 10K smoke; `--long` defers them): `docs/measurements/dense-sync-stack-20261118/submit-5m.sh ARM ROUND`. Ids, round 1 / round 2: dense (9852a3c) 1e5071 / da3323; s2 (35296ba) eebae8 / b03bdd; stacked (d04794d) b6ee71 / 54b693; A/A (stacked tree again, own slot, stage aa) 90a1aa / 8657da; main (ea1fddc) 028406 / 51839b. Round 1 then round 2, interleaved.
3. Risk: main may not read an Oct-era fixture. A1 job b5ef57 at main failed "authority did not elect an available leader" on the 10K Oct-23 fixture. Diagnostic 261118-064115-2e8717 (10K smoke at main, logs kept) says why. If main cannot read old state, cancel the two main arms (028406, 51839b) and the three 100K main arms (71faa7, 8e49b7, 59781b): they fail after a 15 min leader wait each. Same check for stacked (d04794d) at 100K round 1 (15108e).
4. The 100K arms stay as the previous contributor queued them.

## Blocked on

Resolved by D119 (see 5M above). Was: A4 at 5M. `.scratch/capacity-trials/5000000-flat-r1` (the 5M fixture of 11-07 and A9) is
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
