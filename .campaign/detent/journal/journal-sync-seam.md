# journal-sync-seam: journal

State: waiting on jobs 261118-002316-5ee759 (suite at journal:ce05f0e) 261118-002319-ef7067 (A1 grep)
Quest: .campaign/detent/quests/journal-sync-seam.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-sync-seam (branch quest/detent/journal-sync-seam)

## Doing

Merged origin/main (CHANGELOG kept both entries; turn_log.h merged clean). TurnDriver::commit now calls `log_.flush(write)` (journal:ce05f0e, pushed), so no sync bypasses the seam. docs/MEASUREMENTS.md has the open-cost section (52.9 -> 6.4 ms per group); the merge did not touch the open path, so no rerun. turn-seal.md names the seam barrier.

## Next

1. Read the suite and the A1 grep; fill the Evidence table; set "ready for review".

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`).

| Criterion | Evidence |
|---|---|
| A1 | grep job 261118-002319-ef7067 at journal:ce05f0e: `! grep -rnE '(->|\.)sync(_data)?\(\)|fsync|fdatasync|F_FULLFSYNC' include --exclude=memory_io.h` (found only turn_driver.h before) |
| A2 | suite 261118-002316-5ee759 at journal:ce05f0e (earlier: 261117-200917-ac87f4 at journal:54d2972 ; cases host_table, turn_segments, turn_flush, journal_tail); fails-without at journal:15f4f0a 261117-185442-6f0142 failed 9 of 13 checks on main's code |
| A3 | suite 261118-002316-5ee759 (the 261117-200917-ac87f4 failure was crc_fixture's timeout, fixed on main) |
| A4 | strict jobs: before 261117-200923-d18138 (main's library, journal:3282210), after 261118-004757-dc99ce; open with seam syncs skipped 52.9 -> 6.4 ms per group (200 groups); line in docs/MEASUREMENTS.md |

## Result

Not final.

## Rulings I made

- One helper, `detail::sync_file(IO&, File&)`, calls `IO::sync_files` with the one file; no new method, 1:1 with the old call and its directory sync, so crash points are unchanged.
- The test's counts: the host-table commit is one seam sync then one directory sync; each prepared segment one seam sync; a torn-tail recovery one seam sync. `journal_tail`'s creation check is `>= 1`, since the manifest publication's `Sync` primitive is the blocked item.
