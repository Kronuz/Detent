# faultfs-collect-linear review: correctness

Commit: faultlab:5f4d1042322f776dff1fbbf2c80ac211058562f7
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261118-091517-054e8f: faultlab suite, 17 of 17, at 5f4d104 (A1's existing tests).
- 261118-091518-7ecf95 and 261118-091520-fd4c94: the durable and log mutants, built with
  FAULTLAB_CHECK_COUNTS=0, fail "collect frees exactly the inodes nothing references". I read
  `.scratch/collect_mutant.sh`: each mutant removes exactly one reference kind (durable names set by
  a directory sync; logged ops), so each targets A2's claim. The by-value case checks `used()` with
  power-of-two sizes plus views, live and after two crash policies: values, not counts.
- 261118-093241-d3f204 (A2's pass at head) and the A4 jobs were still queued; I did not rely on them.

## What I checked myself

Scripts under `.scratch/review-faultfs-collect-linear-correctness/` in the Detent home checkout.

1. 261118-094525-b37304 (`faultlab_counts.sh`): faultlab's whole suite built with
   `-DFAULTLAB_CHECK_COUNTS=1` (every test, not only faultfs_test): 17 of 17 pass, no check fires.
   Then `oracle/trace.cc`, a differential trace: 600 seeds x 250 random ops (create, write,
   truncate, preallocate, sync, replace, link, remove, sync_directory, ordering points, drop/open
   handles, crash under random policies, kill, power_off_after, fault rules before/after, snapshot
   plus a crash of the copy), printing after every step `used()` and each file's inode id, link
   count, size and content hash. Built once against the frozen pre-change faultfs.h
   (`oracle/old/`, origin/main adc4fc2) and once against 5f4d104 with the counts check on.
   530 seeds agree byte for byte. 70 differ, and in every one the first difference is the new
   build's "X not durable after its directory sync" check (F1). No refs/links/used()/unreferenced
   check fired in 151200 steps (25255 crashes, 7389 snapshots).
2. 261118-094526-13b2f2 (`journal_counts.sh`): journal:36912d7 built against this faultlab
   (FETCHCONTENT_SOURCE_DIR_FAULTLAB, 52 build lines carry FAULTLAB_CHECK_COUNTS=1): 57 of 57
   ctest entries pass, no check fires. This covers journal's power-loss tests, turn_seal (threads),
   turn_bound, placement and both fuzz replays; not the reopen_* cases (they run by argument, A4's).
3. 261118-094623-6dee9e (`reentrant.sh`): an ordering point settled before its file stages (F2).
   Old faultfs.h: the ordered bytes are durable after the next sync_directory ("NEW" after a crash
   keeping old sectors); new: "old"; new with checks: "inode 1 staged without an ordering point".

Answers to the lead's questions:

1. Reference kinds: names, durable names, log entries, handles (use_count), replace over an
   existing name (including onto a name of the same inode: `set_name` early return), links,
   remove of an open file (kept in `unreferenced_` until a collect finds it unheld) and truncate
   (the `accounted` guard) are counted on every path I found (faultfs.h:487-530, 1251-1288,
   1424-1428); the trace and both suites never tripped the count check.
2. No inode is freed while a durable name or a log entry names it, and none leaks: check_counts
   (refs==0 implies in `unreferenced_`) held throughout. But the incremental directory sync can
   disagree with the old full copy: F1 changes which names, and so which inodes, survive a crash.
3. `used()` matched the old full sum at every step of every agreeing seed, across crash, recount,
   snapshot, ENOSPC (a third of the seeds run at capacity 24000) and failed syncs.
4. The A1 case checks values and each mutant targets its claim. The counts check is on only in
   faultfs_test, which has no self-replace plus ordering-point case, so it never saw F1 (F3).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A self-replace (`replace(x, x)`, which faultfs allows and logs) followed by an ordering point and a directory sync leaves `x` live but not durable, so a crash loses `x` and its inode even though its directory was synced. `apply()`'s replace erases `e.a` after writing `e.b`, so with a==b it drops the name from `durable_names_` (faultfs.h:583-587). `settle_staged()` applies the entry and erases it from the log (606-607), so `settle_directory()`'s touched set no longer holds `x` (614-630). The old full-directory copy repaired that, so this breaks the quest's "unchanged semantics: which inodes survive a crash". journal's turn_log takes ordering points (`order_files_and_directory({})`, journal turn_log.h:439), so a journal path that renames a file onto itself would hit it. The same `apply()` bug already dropped the name on a crash before any sync. That part predates this change. | 261118-094525-b37304: 70 of 600 seeds diverge, all at this check. Seed 12 has `replace a a` at step 91, an ordering point at 97, and the check fires at 122. 68 of the 70 seeds show a plain self-replace before the first check; the other two (seeds 182 and 328) show a self-replace that a fault rule failed after its effect. | Make `apply()`'s replace a no-op when `e.a == e.b`. That also fixes the crash path. Alternatively, have the live replace skip logging a self-replace. Test: create `a`, sync_directory, `replace("a","a")`, `order_files_and_directory({})`, sync_directory, `crash(names=durable)`, then check that `view("a")` survives. Rerun `faultlab_counts.sh` until the traces agree. |
| F2 | no | defect | R4 assumes staged inodes imply ordering points. `order_files_and_directory` unlocks between pushing the mark and staging its files, so a sync_directory in that gap (from another thread, or a foreign File's `sync()`) settles and clears the mark, and the file is staged with no point. The old `settle_directory` settled such an inode at the next directory sync. The new one never does, so the ordered bytes can be lost after "durable at the next sync_directory". With checks on, it throws. Only concurrent or re-entrant use reaches it, and it costs a spurious failure, not a false pass. I found no consumer that passes files to an ordering point concurrently (journal passes `{}`). | 261118-094623-6dee9e; faultfs.h:1325-1349 and 1132-1144. | Options: `stage()` settles at once when `epoch >= marks_.size()`, or a staged-inode set that `settle_directory` settles in O(staged); or keep the behavior and document it. |
| F3 | no | gap | `FAULTLAB_CHECK_COUNTS` is on only in faultfs_test, and no suite test is an oracle against the old model. The differential trace found F1, which the scenario cases missed. | `test/faultfs_test.cc:7-9`; CMakeLists.txt:28-33 | Add a seeded differential or property case (for example, after every directory sync, names under it equal durable names) to faultfs_test, and turn the define on for every `FAULTLAB_TESTS` target. |
| F4 | no | nit | Journal rule R5 says journal does not use ordering points. It does (turn_log.h:439), so each of its ordering points makes the next sync recount everything. The old settle walked every inode too, so the order of cost does not change, but A4's reasoning should not rest on R5. | journal:36912d7 include/journal/turn_log.h:439 | Correct R5 in the journal. |
| F5 | no | nit | A moved-from filesystem keeps its old `used_` while its `inodes_` is empty (faultfs.h:852-859). Only reachable through a handle into a moved-from model, which the API already forbids. | faultfs.h:852-859 | Optional: zero `o.used_` in `take()`. |

## Not run

- journal's reopen_distinct, reopen_heap and reopen_churn with checks on (A4's jobs run them
  without checks; still queued at review time).
- consensus and Xapiand suites on this faultfs.
- Sanitizers and Linux (CI's). CI at 5f4d104 was still running (GitHub returned 502 to `campaign status`).
- Map_shared and media faults in the trace (neither changes a footprint or a name).
- Performance (A3) beyond reading the gate; not this lens.
