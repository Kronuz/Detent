# faultfs-collect-linear: journal

State: waiting on jobs 261118-100249-d31056 261118-100250-ab97bb 261118-100252-f99fe2 261118-100253-64a86e 261118-100254-d58e49 261118-100256-afaa01 261118-100257-47f46c 261118-100259-7ca44a 261118-100306-83c3ca
Quest: .campaign/detent/quests/faultfs-collect-linear.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Lead's ruling (relayed in session, quest file updated): if a run's cost grows with the live inode count,
count references per inode and keep used() as a running total. Probe 261118-090616-a8730a showed it does:
in reopen_distinct, names/durable/inodes reach 4708 by collect 10000 and 8126 by 18000, the log stays
at 0-2 entries, there are no marks, everything is in 1 directory, and 3000 ids take 73.99 s against
7.77 s at 1000. Sample in 261118-090506-35f0ce: settle_directory (memcmp, rebuilding the durable map)
and collect at the top. So faultlab:28c0396 counts references:

- Each inode counts its refs and links.
- collect() frees an unreferenced set.
- A directory sync makes durable only the paths its logged ops touched.
- used() is a running total, which an RAII guard updates on each resizing file call.
- A crash, a settled ordering point and a copy recount everything.
- FAULTLAB_CHECK_COUNTS=1, set in faultfs_test, checks every collection against a full recount.

Docs: docs/mechanisms/crash-model.md (faultlab:5f4d104).

## Review round 1 (reviews/faultfs-collect-linear-correctness.md, CHANGES)

- F1 fixed at faultlab:fcbe2a9: apply()'s replace is a no-op when source equals destination, in
  settling and in crash replay. Case "a self-replace stays durable across an ordering point and its
  directory's sync". The reviewer's trace diff (my copy: faultlab worktree
  `.scratch/trace-oracle/trace_diff.sh`) now compares against the old faultfs.h patched with the
  same one-line fix, which must agree byte for byte on all 600 seeds. It also compares against the
  unpatched old header: each seed that differs must show a self-replace before its first
  difference, because the fix changes the old crash path's replay too (ruling R6).
- F2 introduced by this change, not pre-existing: 261118-094623-6dee9e at 5f4d104 shows the old
  faultfs.h "NEW" and the new one "old". Baseline job 261118-100306-83c3ca runs the same probe at
  main adc4fc2. Fixed at fcbe2a9: stage() lists an inode staged after its point settled in
  `strays_`, the next settle_directory settles them in O(strays), and a crash or a settled point
  clears them. The copy and the move carry them. Case "an ordering point settled before its file
  stages: the next sync_directory settles the file".
- F3: added the property half to faultfs_test: 200 seeds x 250 random ops with the counts checks on
  (case "random operations keep every count and each synced directory's names"). It fails without
  F1's fix (job f99fe2). Turning FAULTLAB_CHECK_COUNTS on for the other FAULTLAB_TESTS targets needs
  CMakeLists.txt, outside this file set: one `target_compile_definitions(... FAULTLAB_CHECK_COUNTS=1)`
  per test target. Left for the lead.
- F4: R5 corrected below.
- F5 (nit, optional): not taken.

## Next

1. When the jobs finish, fill the Evidence table. Check the A4 logs' `faultlab built:` commit
   (fcbe2a9; a3b71d1 changes only docs).

## Blocked on

Nothing.

## Evidence

The branch is pushed at faultlab:5f4d104, and CI is running. A4 jobs build journal:36912d7 with
`--ref faultlab=<worktree>@28c0396`. Each log prints the ref's commit, the faultfs.h it compiled,
FETCHCONTENT_SOURCE_DIR_FAULTLAB and the build.ninja include lines. `campaign brief` flags A4 jobs as
"ran in journal, not the quest's repository": they are the --ref runs the lead asked for.

Earlier design (faultlab:ce3b6b6, one gather per call), superseded:
- The suite passed (1483dd).
- The durable and log mutants were caught (ded7b6, a500b6).
- Per inode per call: 90 / 115 / 188 ns at 1K / 10K / 100K (c1a475).
- The old cubic collect failed the 2x gate at 9429 / 177601 ns (f98716).
- reopen_heap finished in 51.90 s with 248.469 B per group before settle and 248.452 B after (66f9b2).
- reopen_distinct and reopen_churn were cancelled for this redesign.

Round 1 jobs. Run at fcbe2a9 unless noted; a3b71d1 changes only docs.

| Job | What |
|---|---|
| 261118-100354-0b4a4b | suite at a3b71d1 (campaign verify): 17 of 17 |
| 261118-100312-e036a3 | suite rerun at fcbe2a9: 17 of 17 |
| 261118-100249-d31056 | F1 case passes |
| 261118-100250-ab97bb | F1 case fails without the fix (mutant selfreplace) |
| 261118-100252-f99fe2 | random-ops case fails without F1's fix |
| 261118-100253-64a86e | F2 case passes |
| 261118-100254-d58e49 | F2 case fails without the strays settle (mutant strays) |
| 261118-100256-afaa01 | random-ops property case passes |
| 261118-100257-47f46c | trace diff, 600 seeds x 250 |
| 261118-100259-7ca44a | reviewer's reentrant probe at the fix |
| 261118-100306-83c3ca | reviewer's reentrant probe at main adc4fc2 |
| 261118-100313-d0a14e | A2 by-value case: ok |
| 261118-100315-5c543c, 261118-100316-19d609 | A2 mutants (durable, log): the by-value case fails in both |
| 261118-100319-9bf4ae | A3 publish cost: 2.35 / 3.61 / 6.47 us at 1K / 10K / 100K |
| 261118-100320-f185fe | A3 mutant (recount): 387.55 -> 6492.35 us, the gate fails |
| 261118-100321-77c241 | rerun of f98716 (ce3b6b6's `cubic` mutant): exit 126, the mutant no longer exists in collect_mutant.sh; superseded by f185fe, drop it from A3 |
| 261118-100322-ca61d8, 261118-100323-9b1fea, 261118-100324-bf771a | A4 at fcbe2a9 (`faultlab built:` checked): distinct slope 0 B/group, heap 248.469 -> 248.452 B/group, churn 0 failures |

F1=<pending: 261118-100249-d31056 + 261118-100250-ab97bb + 261118-100257-47f46c>

Before round 1 (at 5f4d104):

| Criterion | Evidence |
|---|---|
| A1 | 261118-091517-054e8f (suite, 17 of 17, at 5f4d104) |
| A2 | passes at 5f4d104: 261118-093241-d3f204 (by-value case alone); fails without: 261118-091518-7ecf95 (durable names uncounted), 261118-091520-fd4c94 (logged ops uncounted), built with FAULTLAB_CHECK_COUNTS=0 so the by-value case alone catches them |
| A3 | 261118-091525-a25512 (5.29 / 6.64 / 9.66 us per publish at 1K / 10K / 100K live files, Release, checks off); fails without 261118-091523-021b88 (collect() recounts everything) |
| A4 | faultlab-quest jobs at 5f4d104, journal pinned at 36912d7 via --ref and built against the job's faultlab checkout: 261118-093254-b211a1 distinct, 261118-093255-5aec56 heap, 261118-093256-dcbd77 churn (queued; timeout 1440 s). Earlier journal-repo runs at faultlab:28c0396 (not counted): 999d92 slope 0 B/group, 5ff857 248.45 B/group, 93f1f4 churn 824 B |

## Result

Pending.

## Rulings I made

- R1 by-value check: each inode in the new case has a distinct power-of-two size, so `used()`
  (public) names the surviving set exactly, live and after crashes with durable names and with
  the logged additions.
- R2 The scaling case runs only when FAULTLAB_COLLECT_SCALING=<max live files> is set, so the suite
  times nothing. Its gate: the time per publish (create, write 100 B, sync, sync_directory, replace
  onto a live name, sync_directory) at most doubles per decade. It takes the best of 5 rounds of 64
  in Release with FAULTLAB_CHECK_COUNTS=0, and the case fails if the checks are compiled in.
- R3 FAULTLAB_CHECK_COUNTS is a compile-time opt-in that faultfs_test turns on, not tied to NDEBUG.
  journal's suite builds without a build type, so with NDEBUG its heap cases would pay a full recount
  on every operation. Only faultfs_test turns it on: the other faultlab tests are outside this file set.
- R4 settle_directory no longer settles staged inodes after settle_staged(). An inode is staged only
  while an ordering point exists, and settle_staged() settles all of them and clears the points.
  check_counts asserts that no inode is staged when there are no ordering points.
- R5 settle_staged (ordering points) and crash recount everything. Both already walk every inode,
  as the old settle did, so the order of cost is unchanged. journal does take ordering points
  (turn_log.h:439, `order_files_and_directory({})`; review F4), so each of its points makes the
  next sync recount; A4's runs measure that cost and do not depend on journal avoiding points.
- R6 The trace-diff gate compares against the old faultfs.h plus the same self-replace fix. The
  old apply() bug also dropped a self-replaced name in crash replay, so exact agreement with the
  unpatched header would require keeping the bug. Against the unpatched header the job requires that
  every differing seed show a self-replace before its first difference.
- R7 F2 fix: a set of stray inode ids (`strays_`), not an inode walk, so settle_directory stays
  O(touched + strays). A stray keeps its epoch number, as in the old model.
