# faultfs-collect-linear: journal

State: waiting on jobs 261118-090506-35f0ce 261118-090508-66f9b2 261118-090511-5f552f 261118-090524-f98716 261118-090526-c1a475 261118-090616-a8730a
Quest: .campaign/detent/quests/faultfs-collect-linear.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at faultlab:ce3b6b6 (quest/detent/faultfs-collect-linear): collect() gathers referenced ids
into an unordered_set once per call. Prototype job 261118-084810-2701ea (same algorithm, std::set):
reopen_distinct 400 ids 1.67 s (was 24.51 s), 1000 ids 8.75 s, 10000 ids timed out at 300 s, so a
run stays superlinear: collect per call is linear but runs per op, and write/preallocate call
used_locked() (a scan of every inode) on each growing write. Probe 261118-090616-a8730a prints what
grows (names, durable, log, inodes) and the scan counts.

## Next

1. Read probe 261118-090616-a8730a; if live inodes/names grow with groups, make the per-op scans
   incremental (collect only the inodes an op unreferenced; a running used total), within faultfs.h.
2. Read A4 jobs 261118-090506-35f0ce (distinct), 261118-090508-66f9b2 (heap), 261118-090511-5f552f
   (churn), timeout 1440 s, which sample the process at 60 s.
3. Docs done: docs/mechanisms/crash-model.md (faultlab:0739b3a).

## Blocked on

Nothing.

## Evidence

Branch pushed (faultlab:0739b3a, CI). A4 jobs build journal:36912d7 against
`--ref faultlab=<worktree>@ce3b6b6`; 35f0ce's log shows the ref at ce3b6b645113, its faultfs.h
with the linear collect, FETCHCONTENT_SOURCE_DIR_FAULTLAB set to it and 14 build.ninja include
lines from it.

| Criterion | Evidence |
|---|---|
| A1 | suite 261118-090504-1483dd at faultlab:ce3b6b6, exit 0 (includes "collect frees exactly the inodes nothing references") |
| A2 | --fails-without 261118-090519-ded7b6 (durable refs dropped) and 261118-090521-a500b6 (log refs dropped): only the new case fails, used() off and map::at on the crashed name |
| A3 | pending: 261118-090526-c1a475 (1K/10K/100K, Release), old-collect mutant 261118-090524-f98716; mutant runs show 85-94 ns/inode at 1K, 114-137 at 10K |
| A4 | pending: 261118-090506-35f0ce, 261118-090508-66f9b2, 261118-090511-5f552f (timeout 1440 s each); risk: prototype 261118-084810-2701ea timed out at 300 s on reopen_distinct 10000 |

## Result

Pending.

## Rulings I made

- R1 by-value check: each inode in the new case has a distinct power-of-two size, so `used()`
  (public) names the surviving set exactly, live and after crashes with durable names and with
  the logged additions.
- R2 the scaling case runs only with FAULTLAB_COLLECT_SCALING=<max inodes>, so the suite does not
  time anything; its gate is ns per inode per collect within 2x per decade, best of 5 rounds of 8
  removes, Release.
- R3 hash set over a sorted vector: expected linear, as the quest asks; a sort would be n log n.
