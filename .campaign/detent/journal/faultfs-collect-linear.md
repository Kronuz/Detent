# faultfs-collect-linear: journal

State: working
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
3. Docs: crash-model.md, what keeps an inode alive.

## Blocked on

Nothing.

## Evidence

Jobs at faultlab:ce3b6b6: suite 261118-090504-1483dd (A1); mutants durable 261118-090519-ded7b6,
log 261118-090521-a500b6 (A2, --fails-without); scaling 261118-090526-c1a475 and old-collect mutant
261118-090524-f98716 (A3).

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |
| A4 | pending |

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
