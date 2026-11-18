# journal-sync-seam: journal

State: waiting on jobs 261117-185442-6f0142 261117-185506-392924; then on completion-sync-data (D113)
Quest: .campaign/detent/quests/journal-sync-seam.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-sync-seam (branch quest/detent/journal-sync-seam)

## Doing

Sync-API sites routed through `IO::sync_files` via `detail::sync_file` (primitives.h), journal:14515ab:
host `replace_file` (the host-table commit), `TurnLog::preallocate`, `Journal::create` header, `Journal::recover` torn-tail truncate.
Test: test/sync_seam_test.cc (journal:15f4f0a, test only; journal:14515ab, with the fix). CountingIO counts seam calls and each `File::sync` past them.

## Next

1. Read the fails-without job 261117-185442-6f0142's log: it must fail on the test's own checks, not on a build error; fix a compile error if any. Read the suite 261117-185506-392924.
2. After completion-sync-data lands: repin completion, route `sync_data` sites (turn_log.h, turn_driver.h) through `io.sync_data_files`, add the turn flush case to sync_seam_test, A1 grep job, A4 at the strict level (never exclusive), perf line in docs/.

## Blocked on

Nothing for the lead. D113 (option A) ruled: completion-sync-data adds `IO::sync_data_files` and routes the executor's Sync and SyncData through the seam; this quest lands after it. Steps 2 above wait for it.

## Evidence

Jobs prove criteria by their tags (`--for A1`).

| Criterion | Evidence |
|---|---|
| A1 | blocked: grep job after the fork above; today's direct sites: turn_log.h (sync_data x2), turn_driver.h (sync_data), and the Sync/SyncData primitives in artifact.h, mutation.h, turn_driver.h |
| A2 | queued: fails-without at journal:15f4f0a 261117-185442-6f0142; suite with the fix at journal:14515ab 261117-185506-392924. Covers host table, segments, torn tail; the turn flush case is blocked |
| A3 | queued: suite 261117-185506-392924 |
| A4 | blocked: needs the turn flush routed; Xapiand's footprint reproducer is Xapiand:d5b84cdf4's group_footprint_test |

## Result

Not final.

## Rulings I made

- One helper, `detail::sync_file(IO&, File&)`, calls `IO::sync_files` with the one file; no new method, 1:1 with the old call and its directory sync, so crash points are unchanged.
- The test's counts: the host-table commit is one seam sync then one directory sync; each prepared segment one seam sync; a torn-tail recovery one seam sync. `journal_tail`'s creation check is `>= 1`, since the manifest publication's `Sync` primitive is the blocked item.
