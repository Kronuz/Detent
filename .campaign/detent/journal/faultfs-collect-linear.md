# faultfs-collect-linear: journal

State: waiting on jobs 261118-091517-054e8f 261118-091518-7ecf95 261118-091520-fd4c94 261118-091523-021b88 261118-091525-a25512 261118-091528-999d92 261118-091530-5ff857 261118-091533-93f1f4
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

## Next

1. Read the jobs above; fill the Evidence table; State "ready for review".

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

| Criterion | Evidence |
|---|---|
| A1 | pending: suite 261118-091517-054e8f at 5f4d104 |
| A2 | pending: --fails-without 261118-091518-7ecf95 (durable names uncounted), 261118-091520-fd4c94 (logged ops uncounted); built with FAULTLAB_CHECK_COUNTS=0, so the by-value case alone must catch them |
| A3 | pending: 261118-091525-a25512 (publish cost at 1K/10K/100K live files, Release, checks off); --fails-without 261118-091523-021b88 (collect() recounts everything) |
| A4 | pending: 261118-091528-999d92 distinct, 261118-091530-5ff857 heap, 261118-091533-93f1f4 churn (timeout 1440 s) |

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
  and journal does not use ordering points.
