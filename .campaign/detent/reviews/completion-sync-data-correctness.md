# completion-sync-data review: correctness

Commit: completion:f4c19b1cf8c28973a5c0f5147134fd955546f210
Verdict: APPROVE
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261117-194634-51b4f9 (suite, A1/A2/A4) and 261117-194637-12c92c (A3, `completion_seam_shape`) passed at this commit.
- 261117-194643-ade55a (A2 fails-without): the mutant restores main's `operation.h` (merge base completion:7731315d0660), builds, and fails at a value check, not a crash: `failed: each async sync reaches the seam ... through the inline loop`, got `n:sync n:sync_data n:sync_data n:sync`, want four `seam:` lines.
- CI green per the lead; `campaign status` still reads "ci not run at head", so the lead confirms the CI record before landing.

## What I checked myself

Answers to the lead's questions, by reading the diff (`git diff origin/main...HEAD` in the completion worktree) and one probe job.

1. Per-platform semantics. `PosixIO` does not override `sync_data_files`, so an executor `SyncData` is `IO::sync_data_files` (io.h:239), then `File::sync_data`, then `PosixFile::sync_data` (posix.h:539), then `file_sync_data` (posix.h:184). That is `fdatasync` on Linux, `F_FULLFSYNC` on macOS and `fsync` elsewhere, the same code path as main's direct `request.file->sync_data()`. Foreign files keep their own `sync_data`: sync_seam_test `posix()`.
2. macOS `Sync`. Main ran `file_sync`: one `F_FULLFSYNC` (posix.h:176). The branch runs `PosixIO::sync_files` (posix.h:776), which issues `fstat`, `fsync`, then `F_FULLFSYNC` through `sync_calls_`. Probe 261117-200150-0b4c3b confirms the sequence `fsync F_FULLFSYNC`. Over 300 interleaved 4 KiB write-and-sync rounds (light), the medians were 41535 us for `F_FULLFSYNC` alone, 41850 us for `fsync+F_FULLFSYNC` and 41868 us for the executor `Sync`, about +0.8% and within light-hold noise. Durability is unchanged: the last call is the same `F_FULLFSYNC` on the same fd. For one file the `fsync` is redundant (F_FULLFSYNC does fsync's work and then flushes); it pays off only when several files share one device flush. Not a regression.
3. Null file. Main dereferenced a null `Sync` file (UB) and already threw `invalid_argument` for `SyncData`. No caller passes null: journal artifact.h:259, mutation.h:57 and :338, and turn_driver.h:80 all set the file, as does completion image.h:334 after Create. The throw sits inside `execute_primitive`'s `try` (operation.h:186 `catch (...)`, a noexcept function), so on a worker it becomes `result.error`. The Linux ring returns `false` for a null file (linux_completion.h:280), which falls back to that same worker. The test covers both routes.
4. Paths. `drive_synchronously` (operation.h:202), `HeldCompletion` (seam.h:129) and both queues' workers (linux_completion.h:352, bsd_completion.h:220) all call `execute_primitive`, which is the seam. The Linux ring and FreeBSD `aio_fsync` paths for a `PosixFile` bypass the IO; completion-seam.md documents this (see N2). A backend that overrides only `sync_files` gets the `File::sync_data` default for `SyncData`. That is coherent in completion, but see N1 for journal.
5. The A2 test checks values. Each route's ordered log names the seam method, the batch size and the file identity, and records that the file itself is untouched. It also checks the defaults' `sync` and `sync_data` mapping, the errno of a failure, that no step runs after a failure, and that a null file is refused with no seam call. The fails-without job fails on those values.

Probe script, to rerun: `.scratch/review-completion-sync-data-correctness/sync_cost_probe.sh` (with `sync_cost_probe.cc` and `CMakeLists.txt` beside it), submitted as `campaign job submit --quest completion-sync-data -- sh <dir>/sync_cost_probe.sh <dir>`.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | journal's `PrefixIO` forwards `sync_files` but not `sync_data_files`. Once journal repins this commit, the turn flush's `SyncData` through a `PrefixIO` takes the default `File::sync_data` and never reaches the host's IO, so journal-sync-seam's counting host would miss it. This is consumer-side, not a defect here. | journal include/journal/host.h:237; turn_driver.h:80 | journal-sync-seam adds `void sync_data_files(std::span<File* const> f) override { io_->sync_data_files(f); }` to `PrefixIO`, and its A2 counts through a `PrefixIO`. |
| N2 | no | gap | The ring (`IORING_OP_FSYNC`) and `aio_fsync` paths for a `PosixFile` never reach the IO, so a wrapper host over `PosixIO` cannot count or skip async syncs on Linux with a ring or on FreeBSD. The outcome's "every sync, async included" holds only on executor paths (all of macOS, and foreign or memory files everywhere). It is documented. | linux_completion.h:278-285; bsd_completion.h:107; docs/mechanisms/completion-seam.md | Lead's call. (a) Keep it: test hosts use MemoryIO or the worker. (b) Make native sync eligibility an IO decision, at the cost of a new contract and ring syncs moving to the worker for wrapped hosts. |
| N3 | no | nit | macOS executor `Sync` is now `fsync` then `F_FULLFSYNC`, where main issued `F_FULLFSYNC` alone. The extra `fsync` is redundant for one file and costs about +0.8% (noise level); durability is the same. On Linux without a ring, it adds the `sync_file_range` hint. | probe 261117-200150-0b4c3b; posix.h:208-245 | Keep it, or have `files_sync_devices` skip the staging `fsync` for a device that holds one file. |
| N4 | no | gap | No suite test pins the changed `PosixIO` syscall sequence for an executor `Sync`, which the probe shows. An executor `SyncData` on a `PosixFile` cannot be observed through `replace_sync_calls`, because `file_sync_data` calls `::fcntl` and `::fdatasync` directly. This predates the quest. | probe 261117-200150-0b4c3b output line 2; posix.h:173-191 | Optional: add a sync_calls_test case driving `Sync` through `drive_synchronously` that expects `fsync, F_FULLFSYNC`. |
| N5 | no | nit | ARCHITECTURE.md is outside the quest's file set (a `campaign status` warning). | `campaign status completion-sync-data` | The lead widens the file set or drops the edit. |

## Not run

- Linux and FreeBSD: no local run. The ring and `aio_fsync` paths are CI's, and the per-platform claims rest on reading the code.
- No strict or exclusive timing. The N3 numbers come from a light hold and only bound the cost, as within noise.
- No sanitizer runs; those are CI's.
