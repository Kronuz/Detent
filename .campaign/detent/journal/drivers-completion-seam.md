# drivers-completion-seam: journal

State: ready for review once jobs 261116-000744-e8ac8c (suite) and the 8 mutants 261116-000751-c47e20 261116-000752-68fa7c 261116-000752-06ce40 261116-000753-959b2b 261116-000807-2563be 261116-000808-2719f9 261116-000808-b88aed 261116-000809-5174e5 finish green; branch head completion:269a4c4 (not pushed; the lead pushes)
Quest: .campaign/detent/quests/drivers-completion-seam.md (the lead's; never edit it)

## Doing

Code is complete in Kronuz/completion, branch quest/detent/drivers-completion-seam, head completion:f579c4e (code frozen; only docs/MEASUREMENTS.md and the journal remain). Commits: completion:efc368f SyncData, completion:221cc2a seam (completion_ref, InlineCompletion, HeldCompletion), completion:8656f12 shared Asio slot FIFO + token + `lane()` (A4 "before"), completion:38aa41d Turn lane (A4 "after"), docs, test/ helpers, completion:f579c4e lane build step.

Local macOS run: 29/29 tests pass (suite also green as job 261115-224340-12c8ba at completion:2ccce2d, 24 tests). The Linux ring and FreeBSD `aio_fsync(O_DSYNC)` SyncData paths are untested here: no Linux host; CI on push covers them (the ring fault runs under `#ifdef __linux__` in `test/sync_data_test.cc`).

## Next

1. Read the suite and the 8 mutant reruns (each must fail, its `--fails-without` job); the A4 jobs ran at completion:f579c4e, and the later commits touch only test/sync_data_test.cc and docs/MEASUREMENTS.md (`git diff f579c4e HEAD -- include bench` is empty), so the lane code they measured is unchanged.
2. The lead pushes completion:269a4c4 and reads CI: freebsd-x86_64 must pass completion_sync_data (see Rulings).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | suite 261115-230509-d8b5df (green at completion:f579c4e), rerun at completion:269a4c4 as 261116-000744-e8ac8c; mutants at completion:269a4c4: 261116-000751-c47e20 (sync_data runs sync), 261116-000752-68fa7c (error swallowed) |
| A2 | same suites; mutants at completion:269a4c4: 261116-000752-06ce40 (ticket ignored: reorder), 261116-000753-959b2b (Inline ignores token), 261116-000807-2563be (drop delivers), 261116-000808-2719f9 (Held ignores token) |
| A3 | grep 261115-230511-307b75 (at completion:f579c4e; test/sync_data_test.cc is new, so the byte-identical set is unchanged) and the suites; mutant at completion:269a4c4: 261116-000809-5174e5 (a second loop in InlineCompletion) |
| A4 | docs/MEASUREMENTS.md "Turn lane": smoke 261115-225743-cc7228, row 261115-230734-0b0a3e (exclusive, base completion:8656f12 vs head completion:f579c4e), controls 261115-230745-462b77; mutant (lane ignored) at completion:269a4c4: 261116-000808-b88aed |

## Result

Turn lane (docs/MEASUREMENTS.md, completion:269a4c4): 16 Bulk tasks, nine interleaved runs, medians [min, max]. Turn admission wait p50 21.233 [18.330, 25.397] ms (FIFO) to 0.502 [0.474, 0.522] ms (lane), 42-fold; turn total p50 42.8 to 22.0 ms (the no-Bulk floor is 21.7-21.8 ms); total p99 74.2 to 25.0 ms. With 4 Bulk tasks, wait p50 7.596 to 0.544 ms. A/A band, same binary: wait p50 25.992 vs 22.856 ms, runs spanning 18.7-36.4 ms; the effect clears it. Lane off vs base is inside the band; no Bulk tasks, head vs base total p50 21.679 vs 21.813 ms (+0.6%). Verdict: helps turn latency, hurts Bulk throughput (44.1 to 6.7 MiB/s at 16 tasks, -85%; 21.3 to 8.7 at 4, -59%, intervals disjoint): the designed trade, worst case since the probe's turns saturate the slot. macOS only; Linux and FreeBSD unmeasured.

CI at completion:f579c4e (run 43821762431): linux-x86_64-gcc, linux-x86_64-clang, linux-arm64-gcc, linux-asan-ubsan, linux-tsan, macos-arm64, macos-x86_64 passed, so the Linux ring path (`native_ring` in sync_data_test, io_uring where the runner has it) is green under all of them. freebsd-x86_64 failed one test, completion_sync_data: "SyncData is counted once" (28 of 29 passed, including seam, asio_seam and sync_primitives). Cause: `queue_route` also runs the closed-file case, where FreeBSD's `aio_fsync` returns EBADF; BsdCompletionQueue completes that error at once and counts `native_rejected`, not a per-primitive slot, so the sum was 0. A test over-assertion, not a driver defect; Write and Sync refused the same way are uncounted too. Fixed in completion:f2dc801 (sum includes `native_rejected`); the fix is unverified on FreeBSD until the lead pushes and CI runs (no FreeBSD host here).

## Rulings I made

- The token is read at primitive boundaries (before issue, and again when a waiting operation reaches the slot). An issued primitive completes into the operation first; the caller is released then, not during the primitive. D8's "in the kernel: only the caller is released" would need a detached reaper per driver; the boundary rule is safe for every driver and costs at most one primitive's latency. Revisit in journal-driver if a Store call needs earlier release.
- `completion_ref` and the two new drivers live in `seam.h` over `task.hh` only; the core headers stay task-free. `drive_synchronously(op, proceed)` (operation.h) is the one loop; `InlineCompletion::run` calls it.
- Lane is a virtual on the operation (`IOOperation::lane()`, default Bulk), so D2's `run(operation, token)` and every existing operation are unchanged.
- Existing suite edit: `test/asio_extension_test.cc` gains one SyncData step (24 steps). Its per-extension-slot loop covers `completion_extension_count`, which grows to 14, so a kind with no step fails it; every other test file is byte-identical to completion:3f82cda (`test/check_seam.py --base`).
- `tools/` is outside the quest's file set, so the shape grep and the mutation helper live in `test/` (`check_seam.py`, `replace_once.py`).
- Slot discipline (A4): a caller holding the slot between its own primitives keeps it unless a waiter ahead of it (a Turn, or for a Bulk any waiter) is queued; a waiting operation whose token is requested is dropped when the slot reaches it, not earlier.
- The Asio driver admits concurrent runs (the old `active_` refusal is gone): D6 puts every group of a loop on one driver.
- The FreeBSD SyncData failure is fixed in the test, not the driver: refused-at-submission is already counted as `native_rejected` for Read, Write and Sync, so counting it per primitive would change existing behavior. The fix keeps the one-count invariant (`native + fallback + native_rejected == 1`).
- A4 jobs are not rerun at the later head: only a test and docs changed (`git diff f579c4e HEAD -- include bench` is empty). Rerun them if the lead wants the ids at completion:269a4c4.
