# dense-sync-stack: journal

State: waiting on jobs 261118-054316-9f93de 261118-070752-576dde (100K stacked r3, main r3), 5M 261118-064403-1e5071 261118-064414-eebae8 261118-064419-b6ee71 261118-064422-da3323 261118-064424-b03bdd 261118-064425-54b693 261118-064427-8657da, main 5M seed 261118-070640-2fb122 then 261118-070740-6aec23 261118-070741-38688c. Merged origin/main (Detent:8c482ae); A1-A3 and gates carried over by the tool (only merges since). Perf doc written with the 100K table; 5M rows pending. Not yet ready for review: A6 waits on the 5M rows. Blocked on: file-set grant for docs/measurements/dense-sync-stack-20261118/, and the A4 evidence binding (below).
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

1. When 9f93de and 576dde finish: fill the 100K stacked r3 and main r3 rows and medians in docs/perf/dense-sync-stack.md (`python3 <scratch>/sum.py` pattern: SUMMARY lines of `campaign job log`).
2. When the 5M arms finish: write the 5M table (median of 2 rounds per arm, A/A = aa r1 vs r2 and vs stacked), fill main's 5M fixture row in docs/measurements/dense-sync-stack-20261118/README.md (size, records, revision, seed seconds from 2fb122).
3. Then mark "ready for review" (durability).

## 5M (D119)

Fixture: `/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials/5000000-flat-r1` (home .scratch, outside any job checkout; kept, never rebuilt: the seed job exits at once if `seeded.json` is there). Seeded as run_fleet.py did on 2026-10-24 (scenario `.scratch/capacity-trials/5000000.json`, preset capacity-5000000.json, flat writers, seed 17): `docs/measurements/dense-sync-stack-20261118/seed.py` stops after prepare+verify. Arms only `cp -cR` its state-0..2 (coldboot.py), never write it. A failed seed renames it to `.partial`.

1. Seed job 261118-064312-81a63c: light, 135 min (`--long`, deferred: 2026-10-24 seed took 5,484 s), at Detent:9852a3c (oldest arm tree, so the most trees can read it; the harness API of Authority and Writers is the same on all four), `--env FIXTURE=5000000-flat-r1`. On success fill path, size (`fixture-size` in the job out), seed commit and job id into docs/measurements/dense-sync-stack-20261118/README.md and Result below.
2. Arms, `--after` the seed job, exclusive, `--minutes 28 --long` (the campaign caps exclusive at 10 min; a 5M arm is 8-18 min plus a 10K smoke; `--long` defers them): `docs/measurements/dense-sync-stack-20261118/submit-5m.sh ARM ROUND`. Ids, round 1 / round 2: dense (9852a3c) 1e5071 / da3323; s2 (35296ba) eebae8 / b03bdd; stacked (d04794d) b6ee71 / 54b693; A/A (stacked tree again, own slot, stage aa) 90a1aa / 8657da; main (ea1fddc) 028406 / 51839b. Round 1 then round 2, interleaved.
3. Risk: main may not read an Oct-era fixture. A1 job b5ef57 at main failed "authority did not elect an available leader" on the 10K Oct-23 fixture. Diagnostic 261118-064115-2e8717 (10K smoke at main, logs kept) says why. If main cannot read old state, cancel the two main arms (028406, 51839b) and the three 100K main arms (71faa7, 8e49b7, 59781b): they fail after a 15 min leader wait each. Same check for stacked (d04794d) at 100K round 1 (15108e).
4. The 100K arms stay as the previous contributor queued them.

3a. Main diagnosis (msg from lead): job 2e8717's authority logs say `detentd: authority recovery application fenced` on all three voters (service.cc:1983, replay of the committed journal at start), so detentd starts and fences while replaying the Oct-23 journal; it is not a harness or argument mismatch (the harness is the tree's own). The Oct-23 state-N holds journal-0000000000000001, manifest, owner.lock; main also creates empty `state-N.history` beside it (src/detent_authority/history.cc, 65c7ac81, 2026-11-12). Hypothesis: a journal or command format change between Detent:d04794d and main (candidates 53abd637 per-namespace positions and incarnations, 711a114e one-knob-type int64, 65c7ac81 history). Probes (light, 10K, exit code is the bootstrap's): old fixture at d04794d 6c2712, at 53abd637's parent e7158a, at 53abd637 dd5db9, at 711a114e 39af41; fresh 10K seeded at main then bootstrapped at main 08680a (must pass or main really fails to elect). Script docs/measurements/dense-sync-stack-20261118/probe-fixture.sh. If fresh main passes: a fixture format change, greenfield no-compat, so main needs a main-seeded fixture (91 min more at 5M) or the main arm is dropped; name the first bad commit from the probes. If fresh main fails: main regression, bisect between Detent:327ca58 and main.

## Follow-ups for the lead (not this quest)

1. Silent failure on an old journal, whichever way the probes go: main's detentd, given an old-format state, starts, fences while replaying the committed journal and then sits with no leader (the harness gave up after 240 s; the 100K arm waited 900 s). The only output is `detentd: authority recovery application fenced` (src/detent_authority/service.cc:1983, job 261118-064115-2e8717, events.jsonl actor_output). It should refuse the old format at startup with a named error (format and version found, expected) and exit nonzero. Owner: authority, outside this quest's file set.

## Main arms on their own fixture (lead's decision after the probes)

Probes: fresh 10K at main boots main (08680a, done); the Oct-23 fixture boots at d04794d (6c2712) and at 53abd637~1 (e7158a); the 53abd637 and 711a114e probes failed to build (dd5db9, 39af41), so they name no commit. It is a state-format change under no-compat; main refuses nothing by name (follow-up above). Main therefore runs on its own fixtures: `10000-main`, `100000-main` (job b711be) and `5000000-flat-main` (job 2fb122, light, 135 min, `--long`), all in the home capacity-trials, kept. Main arms (`submit-main.sh`, after the seeds): 100K rounds 1-3 d672ef 839960 576dde; 5M rounds 1-2 6aec23 38688c. Old main arms 028406 51839b 71faa7 8e49b7 59781b: cancelled.

## Blocked on

1. File-set grant: docs/measurements/dense-sync-stack-20261118/ (README.md, seed.py, seed-fixture.sh, probe-fixture.sh, submit-5m.sh, submit-main.sh). They are A4's method: the kept-fixture record (D119), the seed scripts the 5M and main fixtures came from, the fixture-format probes, and the scripts that queued every 5M and main arm. docs/perf/dense-sync-stack.md links the README. Recommend granting the directory; the alternative is folding them into docs/perf/ under the perf doc's name.
2. A4 evidence binding: the brief asks to `campaign job rerun` the eight 100K arms at head. Each arm is pinned by design to its own tree (dense Detent:9852a3c, s2 Detent:35296ba, stacked Detent:d04794d, main Detent:ea1fddc); a rerun at the quest head would build the head eight times and erase the comparison. Not rerun. Options: (a) accept the arm jobs as A4 evidence at their arm commits (recommended); (b) rerun only the main arm at the merged head, as "stacked on today's main" (3 x 100K + 2 x 5M exclusive, the 5M ones after a fresh main seed if the state format moved again); (c) restructure as one job per row at head with `--ref` arm checkouts. The queued 5M arms are left as they are (nothing to move: each already runs at its arm tree).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-055013-136e37 (grouped order in the DTPAGE03 test; carried across the main merge); 261118-070747-d672ef, 261118-070751-839960 (100K at main: "publication synced 11 files in 2 groups") |
| A2 | 261118-055013-136e37; mutants 261118-055014-19aed1, 261118-055016-95665a (--fails-without) |
| A3 | 261118-055013-136e37 |
| A4 | 100K: 261118-054306-b5fe77 261118-054307-5a3cb1 261118-054308-15108e 261118-054310-9848a1 261118-054311-2e72e9 261118-054312-c42fcf 261118-054314-792ca7 261118-054315-78f04e 261118-070747-d672ef 261118-070751-839960, pending 9f93de 576dde; 5M: 261118-064421-90a1aa, pending the rest (State) |
| A5 | 261118-055412-a8791e (asan), 261118-055412-1bba5b (tsan), 261118-055412-608f32 (native) |
| A6 | docs/perf/dense-sync-stack.md (100K done; 5M pending) |

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
