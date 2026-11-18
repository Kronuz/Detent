# journal-sync-seam: journal

State: waiting on jobs 261117-200917-ac87f4 (suite) 261117-200923-d18138 (A4 before) 261117-200935-1fd941 (A4 after); then on journal-turn-bound landing
Quest: .campaign/detent/quests/journal-sync-seam.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-sync-seam (branch quest/detent/journal-sync-seam)

## Doing

Repinned completion to f4c19b1 (journal:54d2972, pushed). Turn log barriers go through `detail::sync_data_file` (`IO::sync_data_files`); `TurnLog::flush(write)` is the same barrier for a driver; `PrefixIO` forwards `sync_data_files` (review N1). sync_seam_test gained `turn_flush` and `open_cost [groups]`.

## Next

1. Read the suite and the A4 pair; write the perf line in docs/MEASUREMENTS.md with the numbers.
2. A1: after turn_driver.h:276 (`write.file->sync_data()` in `TurnDriver::commit`, journal-turn-bound's file) becomes `log_.flush(write)`, submit the grep job (see Evidence). Not edited here: not in my file set.

## Blocked on

Ordering, ruled by the lead: journal-turn-bound lands first, then turn_driver.h joins my file set. Then: merge main, change turn_driver.h:276 to `log_.flush(write)`, run A1's grep. PosixIO keeps the default `sync_data_files`; a host IO overrides it (Xapiand's).

## Evidence

Jobs prove criteria by their tags (`--for A1`).

| Criterion | Evidence |
|---|---|
| A1 | pending: `! grep -rnE '(->|\.)sync(_data)?\(\)|fsync|fdatasync|F_FULLFSYNC' include src --exclude=memory_io.h`; today it finds only turn_driver.h:276 |
| A2 | suite 261117-200917-ac87f4 at journal:54d2972 (cases host_table, turn_segments, turn_flush, journal_tail); fails-without at journal:15f4f0a 261117-185442-6f0142 failed 9 of 13 checks on main's code |
| A3 | suite 261117-200917-ac87f4 (the earlier one, 261117-185506-392924, passed at journal:14515ab) |
| A4 | strict jobs: before 261117-200923-d18138 (main's library, journal:3282210), after 261117-200935-1fd941; numbers pending |

## Result

Not final.

## Rulings I made

- One helper, `detail::sync_file(IO&, File&)`, calls `IO::sync_files` with the one file; no new method, 1:1 with the old call and its directory sync, so crash points are unchanged.
- The test's counts: the host-table commit is one seam sync then one directory sync; each prepared segment one seam sync; a torn-tail recovery one seam sync. `journal_tail`'s creation check is `>= 1`, since the manifest publication's `Sync` primitive is the blocked item.
