# journal-sync-seam: journal

State: blocked on lead: the turn flush and async primitives need a seam decision (below); A2 host table, segment and torn-tail syncs are done, suite queued
Quest: .campaign/detent/quests/journal-sync-seam.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/journal-wt/detent/journal-sync-seam (branch quest/detent/journal-sync-seam)

## Doing

Sync-API sites routed through `IO::sync_files` via `detail::sync_file` (primitives.h), journal:14515ab:
host `replace_file` (the host-table commit), `TurnLog::preallocate`, `Journal::create` header, `Journal::recover` torn-tail truncate.
Test: test/sync_seam_test.cc (journal:15f4f0a, test only; journal:14515ab, with the fix). CountingIO counts seam calls and each `File::sync` past them.

## Next

1. When jobs finish: read the suite (A2, A3) and the fails-without job; fix a compile or test error if any.
2. After the lead rules on the fork below: turn flush, async primitives, A1 grep, A4.

## Blocked on

The seam has no data-only sync, and the async primitives bypass it. What I found (all `completion` at the pinned 7731315, outside this quest's file set):

1. Turn flush: `File::sync_data()` in `TurnLog::append`/`write` (turn_log.h), `TurnDriver::commit` and `TurnBarrier` (`PrimitiveKind::SyncData`, turn_driver.h). `IO::sync_files` is whole-file: PosixIO fsyncs it, so routing the flush through it turns Linux fdatasync into fsync on every turn (mtime commit); macOS is the same call either way (F_FULLFSYNC).
2. Async/driver primitives: `PrimitiveKind::Sync` (artifact.h, mutation.h: artifact builds, manifest publication) and `SyncData` run `request.file->sync()`/`sync_data()` in completion's executor (operation.h:155-160), natively through io_uring on Linux. They never reach `IO::sync_files`, so no journal-side call can route them; `SyncFiles` goes through `sync_files_and_directory` (adds a directory sync, off the native ring).
3. Xapiand's skip hook is `PosixIO::sync_calls_`, reached only from the IO methods; `PosixFile::sync` ignores it.

Options:
- A (recommended, one mechanism): completion changes, journal changes nothing more. Add `IO::sync_data_files(span<File* const>)` (default: `sync_data` each; PosixIO routes through `sync_calls_`, fdatasync on Linux). The blocking executor's `Sync`/`SyncData` call `io.sync_files`/`io.sync_data_files` on the one file. The native ring keeps its ops, with PosixIO's hook honored where it submits them. Then journal's `sync_data` sites become `io_.sync_data_files(...)` and its async primitives stay as they are. The method it adds: `sync_data_files`; Xapiand consumes it.
- B (journal only, no new method): route the turn flush through `sync_files` now. Cost: Linux fsync for fdatasync per turn, unmeasured; the async `TurnBarrier` stays on `SyncData` (still bypassing), so the footprint's 15% stays unless it moves to `SyncFiles` (adds a directory sync, leaves the native ring).
- C: make `PosixFile::sync`/`sync_data` honor the owning PosixIO's `sync_calls_` (completion only). Fewest edits; a File-level seam hook rather than an IO-level one.

Recommend A: the executor change covers every async sync (A1 holds by construction), the data-only semantic stays. Name the completion quest and I continue A1/A4 on its pin.

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
