# drivers-completion-seam: journal

State: waiting on jobs 261115-230509-d8b5df 261115-225743-cc7228 261115-230734-0b0a3e 261115-230745-462b77
Quest: .campaign/detent/quests/drivers-completion-seam.md (the lead's; never edit it)

## Doing

Code is complete in Kronuz/completion, branch quest/detent/drivers-completion-seam, head completion:f579c4e (code frozen; only docs/MEASUREMENTS.md and the journal remain). Commits: completion:efc368f SyncData, completion:221cc2a seam (completion_ref, InlineCompletion, HeldCompletion), completion:8656f12 shared Asio slot FIFO + token + `lane()` (A4 "before"), completion:38aa41d Turn lane (A4 "after"), docs, test/ helpers, completion:f579c4e lane build step.

Local macOS run: 29/29 tests pass (suite also green as job 261115-224340-12c8ba at completion:2ccce2d, 24 tests). The Linux ring and FreeBSD `aio_fsync(O_DSYNC)` SyncData paths are untested here: no Linux host; CI on push covers them (the ring fault runs under `#ifdef __linux__` in `test/sync_data_test.cc`).

## Next

1. When the four jobs finish: read 261115-225743-cc7228 (smoke) then the rows 261115-230734-0b0a3e (exclusive A/B) and 261115-230745-462b77 (controls); write "Turn lane" in `docs/MEASUREMENTS.md` with the numbers, job ids, commits (base completion:8656f12, head completion:f579c4e), host and verdict; commit it (docs only).
2. Rerun the 9 mutant jobs at the head (`campaign job rerun 261115-224728-819f14 261115-224732-958849 261115-224736-cd1941 261115-224740-5020c7 261115-225629-cc929a 261115-225635-dfd490 261115-225640-e43a57 261115-225644-8bef23`; at most 4 at once), they ran at completion:2ccce2d and `campaign brief` calls them other code.
3. Paste `campaign brief`'s Evidence table below; set State to ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | suite 261115-230509-d8b5df (pending; sync_data_test: foreign File and PosixFile through drive_synchronously and the native queue, success and EIO; asio_seam_test sync_data_fault; asio_extension_test's SyncData step). Mutants killed at completion:2ccce2d: 261115-224728-819f14 (sync_data runs sync), 261115-224732-958849 (error swallowed) |
| A2 | suite 261115-230509-d8b5df (seam_test, asio_seam_test). Mutants killed at completion:2ccce2d: 261115-224736-cd1941 (ticket ignored: reorder), 261115-224740-5020c7 (Inline ignores token), 261115-225629-cc929a (drop delivers), 261115-225635-dfd490 (Held ignores token) |
| A3 | grep + existing suites byte-identical to completion:3f82cda except asio_extension_test (one SyncData step): 261115-230511-307b75; suite 261115-230509-d8b5df (completion_seam_shape included). Mutant killed at completion:2ccce2d: 261115-225644-8bef23 (a second loop in InlineCompletion) |
| A4 | smoke 261115-225743-cc7228, row 261115-230734-0b0a3e (exclusive: base completion:8656f12 FIFO slot vs head Turn lane), controls 261115-230745-462b77 (A/A band, lane off, no bulk); mutant 261115-225640-e43a57 (lane ignored) killed at completion:2ccce2d. Numbers pending |

## Result

<When ready for review.>

## Rulings I made

- The token is read at primitive boundaries (before issue, and again when a waiting operation reaches the slot). An issued primitive completes into the operation first; the caller is released then, not during the primitive. D8's "in the kernel: only the caller is released" would need a detached reaper per driver; the boundary rule is safe for every driver and costs at most one primitive's latency. Revisit in journal-driver if a Store call needs earlier release.
- `completion_ref` and the two new drivers live in `seam.h` over `task.hh` only; the core headers stay task-free. `drive_synchronously(op, proceed)` (operation.h) is the one loop; `InlineCompletion::run` calls it.
- Lane is a virtual on the operation (`IOOperation::lane()`, default Bulk), so D2's `run(operation, token)` and every existing operation are unchanged.
- Existing suite edit: `test/asio_extension_test.cc` gains one SyncData step (24 steps). Its per-extension-slot loop covers `completion_extension_count`, which grows to 14, so a kind with no step fails it; every other test file is byte-identical to completion:3f82cda (`test/check_seam.py --base`).
- `tools/` is outside the quest's file set, so the shape grep and the mutation helper live in `test/` (`check_seam.py`, `replace_once.py`).
- Slot discipline (A4): a caller holding the slot between its own primitives keeps it unless a waiter ahead of it (a Turn, or for a Bulk any waiter) is queued; a waiting operation whose token is requested is dropped when the slot reaches it, not earlier.
- The Asio driver admits concurrent runs (the old `active_` refusal is gone): D6 puts every group of a loop on one driver.
