# completion-sync-data: journal

State: ready for review
Quest: .campaign/detent/quests/completion-sync-data.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Implemented at completion:d5c6fd4, state-dir fix completion:f4c19b1 (earlier jobs crashed on create_directory) (pushed): io.h sync_data_files, operation.h executor via seam, test/sync_seam_test.cc, check_seam.py grep, docs/CHANGELOG. Done: suite, A3 and CI green at completion:f4c19b1.

## Next

1. When jobs pass, paste `campaign brief` table below and set ready for review; if the fails-without exits 127 it did not build (not a pass).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261117-194634-51b4f9 |
| A2 | 261117-194634-51b4f9; fails without: 261117-194643-ade55a (exit 1 on "failed: each async sync reaches the seam, once, with the file through the inline loop"; 261117-190045-644dbb crashed on the test's own state-dir bug, fixed in completion:f4c19b1, so it is not the kill) |
| A3 | 261117-194637-12c92c |
| A4 | 261117-194634-51b4f9; CI green at completion:f4c19b1 |

## Result

`IO::sync_data_files` (default `File::sync_data` per file) and the executor's `Sync`/`SyncData` through `sync_files`/`sync_data_files`: completion:f4c19b1. Docs: docs/mechanisms/completion-seam.md, ARCHITECTURE.md, CHANGELOG.md. Test: test/sync_seam_test.cc; grep: test/check_seam.py. Native PosixFile paths (io_uring, aio_fsync) stay direct.

## Rulings I made

- Sync through the seam is `sync_files` of the one file (the quest's wording), so on macOS a worker-run Sync on a PosixFile is fsync then F_FULLFSYNC (was F_FULLFSYNC alone); noted in CHANGELOG.
- Native paths (io_uring FSYNC, aio_fsync) for a PosixFile stay the real syscall and do not reach the IO; a counting host with foreign files sees every sync. Documented in completion-seam.md.
- A2 fails-without is a mutant script (test/executor_direct_mutant.sh) restoring main's operation.h, since the test needs the new virtual and would not compile at main; a mutant build failure exits 127 (never a pass).
- A3 is a check in test/check_seam.py (in the suite) over include/, not bench/ or tests.
