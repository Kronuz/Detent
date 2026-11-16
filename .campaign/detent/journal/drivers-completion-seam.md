# drivers-completion-seam: journal

State: ready for review, jobs pending at completion:b8b7f3b (not pushed; the lead pushes): suite 261116-012341-cd69f5; mutants 261116-012344-89db09 261116-012345-ea6cca 261116-012345-7b04f7 261116-012346-79eed8 261116-012346-6a78e9 261116-012347-926a78 261116-012347-8e2ea8 261116-012453-97e70b 261116-012459-d9a1ad 261116-012505-7c4a10 261116-012511-fb3dc5; TSan 261116-012524-159a96; grep 261116-012348-5b0e96; A4 smoke 261116-012348-5d2cfc, rows 261116-012528-382eed (exclusive), controls 261116-012528-ca242d
Quest: .campaign/detent/quests/drivers-completion-seam.md (the lead's; never edit it)

## Doing

Correctness review (CHANGES) folded in at completion:b8b7f3b: completion:6d4cef8 (C2, C6), completion:28e639f (C1, C3), completion:b8b7f3b (docs: C1, C3, C4, C8, C9). Local Debug runs (macOS): asio_seam passes 5 of 5 (cancelled waiter released in 64-85 us; 200 racing rounds), sync_data passes; the C1 mutant (no callback) fails locally: released after 2,000,232 us, "released while the holder's primitive is still blocked" fails.

## Next

1. Read the jobs above; each mutant must exit 0 (killed), the suite and TSan green. A4 rows: compare 261116-012528-382eed with 261115-230734-0b0a3e; if the lane rows move outside the A/A band, update docs/MEASUREMENTS.md.
2. C2 needs CI: the lead pushes completion:b8b7f3b; the Linux jobs (gcc, clang, arm64, asan-ubsan, tsan) must pass completion_sync_data, which now fails on Linux when io_uring is unavailable unless COMPLETION_TEST_NO_IO_URING=1 (CI does not set it). A green Linux run then proves the ring SyncData path and its EIO fault ran.

## Blocked on

Nothing. C2's CI proof waits on the lead's push.

## Review findings (correctness review, CHANGES)

| ID | What I did |
|---|---|
| C1 | `Admit` registers a `cancel_callback` while suspended; it claims the waiter with one atomic (pass_slot claims it too, first claim wins) and posts the waiter's resume to the owner, where it leaves the queue and throws `task::cancelled`; pass_slot skips a claimed waiter. Test `cancel_while_waiting` (asio_seam_test.cc): b waits behind a gated holder, its token is requested from another thread; b must be released while the holder's primitive is still blocked and within 100 ms (64-85 us measured). Fails without: 261116-012459-d9a1ad (no callback), 261116-012505-7c4a10 (pass_slot grants a claimed waiter). Race: `cancel_races_admission`, 200 rounds in the suite, 1000 under TSan in 261116-012524-159a96. Journal ruling corrected (below). |
| C2 | `native_ring` fails on Linux when io_uring is unavailable unless `COMPLETION_TEST_NO_IO_URING=1`; proof is CI after the lead's push. |
| C3 | Lead's ruling: one Turn source per driver. Debug `assert` in `drive` (TurnSource guard) when a second Turn operation starts; documented in the header and completion-seam.md. Test `turn_source_assertion`: control (one Turn, a Bulk beside it, a second Turn after) exits 0 in a child; two overlapping Turns abort with SIGABRT in a child. Fails without the assert: 261116-012511-fb3dc5. `turn_lane` reworked to one Turn operation (it ran two). |
| C4 | MEASUREMENTS "Turn lane" reading now attributes the Bulk drop to the closed loop's turn rate (FIFO gives Bulk ~32 chunks per turn). No new open-loop row. |
| C5 | Not changed: two concurrent runs of one operation are misuse; documenting it is the lead's call if wanted. |
| C6 | Closed-file SyncData checks EBADF (`check_errno`). |
| C7 | Not changed: CI's FreeBSD is 13+. |
| C8 | completion-seam.md states the host drains the queue before destroying it at teardown. |
| C9 | check_seam.py docstring claims only the test/ byte-identity check. |
| C10 | Not changed (review: none required). |

The reviewer's probe jobs 261116-001737-80225e, 261116-002001-ff4d65, 261116-002636-bc1cb7, 261116-003320-48157b failed by probe bugs or one equivalent survivor (review Q2, Q5); they are the review's, not evidence here.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | suite 261116-012341-cd69f5 at completion:b8b7f3b; mutants 261116-012344-89db09 (sync_data runs sync), 261116-012345-ea6cca (error swallowed); Linux ring: CI after the lead's push (C2) |
| A2 | same suite; mutants 261116-012345-7b04f7 (ticket ignored: reorder), 261116-012346-79eed8 (Inline ignores token), 261116-012346-6a78e9 (drop delivers), 261116-012347-926a78 (Held ignores token), 261116-012459-d9a1ad (C1: no cancel callback), 261116-012505-7c4a10 (C1: pass_slot grants a claimed waiter); TSan 261116-012524-159a96 |
| A3 | grep 261116-012348-5b0e96 and the suite; mutant 261116-012347-8e2ea8 (a second loop in InlineCompletion) |
| A4 | docs/MEASUREMENTS.md "Turn lane" (rows at completion:f579c4e: 261115-230734-0b0a3e, controls 261115-230745-462b77); at completion:b8b7f3b (admission hot path changed: a claim CAS per hand-over, a callback per suspended wait): smoke 261116-012348-5d2cfc, row 261116-012528-382eed (exclusive), controls 261116-012528-ca242d; mutants 261116-012453-97e70b (lane ignored), 261116-012511-fb3dc5 (C3: no one-Turn-source assert) |

## Result

Turn lane (docs/MEASUREMENTS.md, completion:269a4c4): 16 Bulk tasks, nine interleaved runs, medians [min, max]. Turn admission wait p50 21.233 [18.330, 25.397] ms (FIFO) to 0.502 [0.474, 0.522] ms (lane), 42-fold; turn total p50 42.8 to 22.0 ms (the no-Bulk floor is 21.7-21.8 ms); total p99 74.2 to 25.0 ms. With 4 Bulk tasks, wait p50 7.596 to 0.544 ms. A/A band, same binary: wait p50 25.992 vs 22.856 ms, runs spanning 18.7-36.4 ms; the effect clears it. Lane off vs base is inside the band; no Bulk tasks, head vs base total p50 21.679 vs 21.813 ms (+0.6%). Verdict: helps turn latency, hurts Bulk throughput (44.1 to 6.7 MiB/s at 16 tasks, -85%; 21.3 to 8.7 at 4, -59%, intervals disjoint): the designed trade, worst case since the probe's turns saturate the slot. macOS only; Linux and FreeBSD unmeasured.

CI at completion:f579c4e (run 43821762431): linux-x86_64-gcc, linux-x86_64-clang, linux-arm64-gcc, linux-asan-ubsan, linux-tsan, macos-arm64, macos-x86_64 passed, and completion_sync_data passed on Linux (asan-ubsan and tsan logs show it; `native_ring` runs there only if the runner's io_uring is available, and ctest's quiet output does not say whether it ran or printed 'ring section skipped'); since completion:6d4cef8 the section fails on Linux without io_uring unless COMPLETION_TEST_NO_IO_URING=1, so a green Linux run shows it ran. freebsd-x86_64 failed one test, completion_sync_data: "SyncData is counted once" (28 of 29 passed, including seam, asio_seam and sync_primitives). Cause: `queue_route` also runs the closed-file case, where FreeBSD's `aio_fsync` returns EBADF; BsdCompletionQueue completes that error at once and counts `native_rejected`, not a per-primitive slot, so the sum was 0. A test over-assertion, not a driver defect; Write and Sync refused the same way are uncounted too. Fixed in completion:f2dc801 (sum includes `native_rejected`); the fix is unverified on FreeBSD until the lead pushes and CI runs (no FreeBSD host here).

## Rulings I made

- The token is read before each primitive is issued. A caller blocked on its in-flight primitive is released when that primitive completes (D8's in-kernel row as amended at Detent:55c77c5a); its wait is that one primitive's latency. A caller waiting for the Asio slot is released at once (C1), not when the slot reaches it: the earlier claim that waiting costs "at most one primitive's latency" was false (the review measured 187-247 ms, 1.4-1.8 s with two Turn sources).
- C1's hop posts the waiter's coroutine handle (`owner_.post`) rather than a `post_call` that unlinks: one hop, and the resumed waiter unlinks itself on the owner. The atomic claim keeps a grant and a cancel from both resuming it; a claimed waiter can resume only by the cancel's post, so the callback never touches a destroyed frame (`~Admit` resets the callback first, which waits for one running elsewhere).
- C3: the assertion is "at most one Turn operation in `drive` at a time per driver", Debug only (no bookkeeping under NDEBUG). Tested by fork: abort is the assertion's outcome, so the scenario runs in a child.
- `completion_ref` and the two new drivers live in `seam.h` over `task.hh` only; the core headers stay task-free. `drive_synchronously(op, proceed)` (operation.h) is the one loop; `InlineCompletion::run` calls it.
- Lane is a virtual on the operation (`IOOperation::lane()`, default Bulk), so D2's `run(operation, token)` and every existing operation are unchanged.
- Existing suite edit: `test/asio_extension_test.cc` gains one SyncData step (24 steps). Its per-extension-slot loop covers `completion_extension_count`, which grows to 14, so a kind with no step fails it; every other test file is byte-identical to completion:3f82cda (`test/check_seam.py --base`).
- `tools/` is outside the quest's file set, so the shape grep and the mutation helper live in `test/` (`check_seam.py`, `replace_once.py`).
- Slot discipline (A4): a caller holding the slot between its own primitives keeps it unless a waiter ahead of it (a Turn, or for a Bulk any waiter) is queued; a waiting operation whose token is requested leaves the queue at once (C1).
- The Asio driver admits concurrent runs (the old `active_` refusal is gone): D6 puts every group of a loop on one driver; at most one of them is a Turn operation at a time (C3).
- The FreeBSD SyncData failure is fixed in the test, not the driver: refused-at-submission is already counted as `native_rejected` for Read, Write and Sync, so counting it per primitive would change existing behavior. The fix keeps the one-count invariant (`native + fallback + native_rejected == 1`).
- A4 jobs rerun at completion:b8b7f3b, since the admission path changed (C1).
