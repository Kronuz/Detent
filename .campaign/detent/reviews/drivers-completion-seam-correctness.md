# drivers-completion-seam review: correctness

Commit: completion:90411863d5e421c99da18568b07432954aa83615 (branch quest/detent/drivers-completion-seam; re-review of the fixes since completion:269a4c4cbfed20a9dbd2616c0e7793adf7a32db4)
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Scope: the fixes completion:6d4cef8, completion:28e639f, completion:b8b7f3b and the merge
completion:9041186, whose only change is the task pin bdafa5f -> 18eb516 (`CMakeLists.txt:32`).
`git diff b8b7f3b 9041186 -- include test` is empty.

## Evidence I relied on

- Jobs at completion:b8b7f3b, all exit 0: mutants 261116-012459-d9a1ad (no cancel callback while
  waiting), 261116-012505-7c4a10 (pass_slot grants a cancelled waiter), 261116-012511-fb3dc5 (no
  one-Turn-source assertion), the quest's eight re-run (261116-012344-89db09 ... 261116-012347-8e2ea8),
  lane mutant 261116-012453-97e70b; TSan 261116-012524-159a96 (1000 racing rounds). The seam's
  include and test trees are identical at 9041186, so they stand for its seam code; the task pin is
  covered by my jobs below.
- CI run 43839792440 at completion:b8b7f3b: all nine jobs green; `completion_sync_data` Passed on
  linux-x86_64-gcc, linux-x86_64-clang, linux-arm64-gcc, linux-asan-ubsan and linux-tsan;
  `completion_asio_seam` Passed on linux-tsan (200 racing rounds). Neither the workflow nor the
  shared `cpp-libraries` workflow nor the log sets `COMPLETION_TEST_NO_IO_URING` (0 matches), so
  each Linux pass ran the ring section (see C2).

## What I checked myself

Scripts under `.scratch/review-drivers-completion-seam-correctness/` in the Detent home checkout.

- Suite at 9041186, Debug, full ctest (job 261116-115020-1ea488): 24/24 passed.
- TSan at 9041186 (task 18eb516), `completion_asio_seam_test 1000` (job 261116-115022-8681d8): clean;
  1000 rounds, 2013 cancelled, 2987 finished; cancelled waiter released in 120 us.
- `rereview.sh` + `rereview_probe.cc` (job 261116-115025-9b0388, Debug + ASan/UBSan, 50 iterations
  each) forces three interleavings the suite reaches only at random or not at all:
  - P1, grant then cancel: the holder's Hold grants b by a post; its awaiting caller resumes first and
    requests b's token on the owner. Release loses the claim; b resumes once, throws `cancelled`
    unissued, and passes the slot to c.
  - P2, a claimed waiter still linked: b's token is requested from a's `on_completed`, so b's claim
    wins and its resume is posted, but b is still linked when a reaches its next Admit. a yields,
    `pass_slot` skips b and grants c; log `a:write c:write a:sync_data`; the slot is free afterwards.
  - P3, legitimate Turn re-entry: a Turn cancelled while waiting, then the next Turn started as soon
    as its caller resumes, driven by two `run_one` calls. No assertion fires.

  Passed on the real code. Both mutants were killed under ASan: `release-ignores-claim` (Release
  posts even when the grant won: double resume, BUS) and `pass-grants-claimed` (SEGV).
- `suite_vs_claim_mutant.sh` (jobs 261116-115204-3e98f5, 261116-115244-944d13; the first printed the
  forked child's expected assertion as the cause, the second the real one): the suite's own
  `asio_seam` test kills `release-ignores-claim` in 5 of 5 runs under ASan (BUS), so the grant-wins
  path is reached by the racing rounds, not only by my probe.
- Read: `asio_completion.h` at 9041186 (Waiter, Admit, Release, pass_slot, TurnSource, drive);
  task 18eb516's `cancel_callback` (`task.hh:1205-1245`: the destructor deregisters and waits for a
  callback running on another thread, invokes inline if already requested, and an empty token
  never runs it) and `asio_scheduler::post` (`task_asio.hh:76-80`: cross-thread safe, dropped once
  closed); `sync_data_test.cc:197-208`; the task log bdafa5f..18eb516 against every task symbol the
  seam uses.

### Q1 (C1): cancel while waiting for the slot

Fixed. Each primitive's Admit has a fresh Waiter whose atomic state goes `waiting` to `granted`
(by `pass_slot`, owner) or `cancelled` (by Release, any thread) through one CAS, so exactly one wins:

1. **Released once, never resumed after cancellation.** Cancel wins: only Release posts the handle;
   `pass_slot` skips the waiter. Grant wins: only `pass_slot` posts; the waiter resumes holding the
   slot and the re-check after Admit throws, if the token's bit is visible by then; otherwise the
   request counts as arriving after admission, and the next pre-check reads it.
2. **No slot leak.** A cancelled waiter never holds the slot. `pass_slot` loops past claimed waiters
   and clears `slot_taken_` only when both queues are empty, so the slot is never free while a live
   waiter is queued (P2). A granted-then-cancelled waiter sets `holding` and its `Hold` passes the
   slot on (P1).
3. **No use after free.** Release copies the handle and `owner_` before its CAS and touches nothing
   of the Admit after it. `~Admit` resets the callback before leaving the queue, and that reset
   waits for a Release running elsewhere. A post that lands after teardown closed the scheduler is
   dropped.
4. **The window between the pre-check and registration.** It is covered: `cancel_callback` runs
   Release inline when the token is already requested.

TSan is clean on macOS (1000 rounds) and on Linux CI (200 rounds).

### Q2 (C2): the ring SyncData section on Linux

Fixed. Without io_uring the section prints the reason to stderr and `std::exit(1)` unless
`COMPLETION_TEST_NO_IO_URING=1` (`sync_data_test.cc:200-207`), so ctest fails rather than skips. With
io_uring it passed on all five Linux CI jobs at b8b7f3b with no opt-out set anywhere, so the ring
SyncData path and its EIO fault ran. The fail branch itself is never executed (no ring-less host); it
is seven lines and read only.

### Q3 (C3): one Turn source per driver

Fixed. `TurnSource` is constructed in `drive` (`asio_completion.h:265-267`), the one body behind
`run` and `run_one`, after the misuse checks that throw and before `Hold`. Its destructor runs on every
exit: completion, exception, cancellation, and frame destruction at teardown. Locals die before
`final_suspend`, so the flag is clear before the caller resumes. Legitimate re-entry (sequential
`run_one` steps, a new Turn right after a cancelled one) does not fire it (P3). The suite's control
in the child process covers a Turn with a Bulk beside it and a second Turn after it. The killed mutant
261116-012511-fb3dc5 shows the assertion test is live. The assertion exists in Debug only; CI's build
type decides whether CI runs it, and the local Debug suite does.

### Q4: the task pin bdafa5f -> 18eb516

Nothing the seam relies on changes behaviour. The seam uses `task`, `cancel_token`,
`cancel_callback`, `cancelled`, `current`, `schedule_on`, `scheduler_ref`, `asio_scheduler` and
`await_asio`:

1. **Cancellation.** `cancel_callback`'s contract is unchanged; `cancel_state` gains a timed variant
   and a refactored `link`.
2. **Deadlines.** They now fire on the scheduler's wheel (the owner thread), so Release then runs
   inline on the owner. P2 exercises that path.
3. **`scheduler_ref`.** It packs a heavy-lane bit, and `operator==` still compares `self_` only, so
   `current() != owner_` is unaffected.
4. **Posting.** `post` dispatches by that bit; the driver's `owner_` comes from `scheduler_ref::of`,
   which is light.
5. **`await_asio`.** It skips inline resume for heavy refs only.
6. **`schedule_on`.** Unchanged.
7. **18eb516 itself.** It touches `demote`'s thread check only.

The suite and TSan pass at 9041186. One consequence is N3.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no (resolved) | defect | Fixed: a caller cancelled while waiting for the slot is released by a `cancel_callback` claim, once, unissued, without leaking the slot; race-free against a concurrent grant | 28e639f; mutants 261116-012459-d9a1ad, 261116-012505-7c4a10; TSan 261116-012524-159a96, 261116-115022-8681d8; probe 261116-115025-9b0388 (P1, P2, two more mutants killed) | none |
| C2 | no (resolved) | gap | Fixed: the ring section exits 1 on Linux without io_uring unless opted out; CI's five Linux jobs ran it and passed | 6d4cef8; `sync_data_test.cc:200-207`; CI 43839792440 | none |
| C3 | no (resolved) | gap | Fixed: one Turn source per driver documented and Debug-asserted in `drive`, the path every run takes; no false fire on sequential re-entry | b8b7f3b; `asio_completion.h:229-246,265-267`; mutant 261116-012511-fb3dc5; probe P3 | none |
| C4, C6, C8, C9 | no (resolved) | gap/nit | MEASUREMENTS now explains the closed-loop Bulk drop; the closed-file case checks EBADF; the host drain at teardown is stated; `check_seam.py`'s docstring matches the code | diff 269a4c4..9041186; suite 261116-115020-1ea488 | none |
| C5, C7, C10 | no | nit | Unchanged from the first review (two drives of one operation interleave; FreeBSD < 13 `aio_fsync(O_DSYNC)` EINVAL; no ring test tells SyncData from Sync) | first review | as stated there |
| N1 | no | nit | `turn_running_` and `TurnSource` exist only without NDEBUG, so `AsioCompletionDriver`'s layout differs between Debug and NDEBUG translation units: a program mixing them breaks ODR with a real size skew, not only a differing assert | `asio_completion.h:229-246` | keep the bool unconditionally (one byte) and make only the `assert` conditional |
| N2 | no | nit | The grant-then-cancel and claimed-but-linked interleavings are reached by the racing rounds (the suite kills `release-ignores-claim` 5/5), not by a deterministic test | jobs 261116-115244-944d13, 261116-115025-9b0388 | optionally lift P1 and P2 from `rereview_probe.cc` into `asio_seam_test.cc` |
| N3 | no | nit | With task 18eb516's heavy lane, the driver resumes waiters and yields through `owner_`, a light ref, so a caller running in a heavy scope continues in the light lane after a slot wait or the 16-step yield; nothing uses heavy with the driver today | `asio_completion.h:84,224,274`; `task.hh:209` | none now; when a heavy-lane IO caller appears, post with the caller's lane (`current().with_heavy(...)`) |

## Not run

- CI at completion:9041186: run 43942262357 was still queued when I wrote this. CI at b8b7f3b is
  green, and 9041186 differs only by the task pin, which my macOS suite and TSan jobs cover; Linux
  and FreeBSD with task 18eb516 rest on that CI run.
- The ring section's failure branch on a ring-less Linux host (no such host).
- The A4 lane rows were not rerun: the seam's include and bench trees are unchanged since they ran.
