# journal-blocking-wake review: correctness

Commit: journal:9e11713edbb6e31681e1ae7a3f8f18f15eb7eaac (quest/detent/journal-blocking-wake)
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-165904-295eae, `journal_driver_test entry_wake_threads` at 9e11713: exit 0, 100 of 100
  records durable, slowest round 3476 us.
- 261119-165717-173c61, the same test at test-only journal:449553d (`--fails-without`): exit 1 on
  "no round waits for its deadline", slowest round 8200612 us, run 819 s for 100 rounds (8.2 s
  each, inside its 20-minute budget). The round's latency check fails, not a job timeout and not
  the 10 s deadline: without the post, the entry wakes only when the wheel next asks for a pass
  (about 8.2 s here), then sees `idle()` false and commits. That is the lost wake the quest names.
- 261119-165908-3668c1, TSan at 9e11713: `journal_driver` runs every case with no argument
  (test/driver_test.cc main), so `entry_wake_threads`, a writer thread calling `wake()` across
  threads into the sleeping entry, ran under TSan with no report; plus the threaded turn_seal
  cases (`wake_threads`, `lock_order`, `unseal_threads`, `forget_threads`, `repeat_threads`).
- 261119-165912-fca4c3, the suite at 9e11713: exit 0.

## What I checked myself

Code reading at 9e11713 and of task's `blocking_scheduler` at the pinned task:95798a0 (copy under
`.scratch/detent/review-journal-blocking-wake-correctness/task-pin.hh`). No probe job: each
question settled by reading.

1. **Lost wake, blocking entry.** `wake()` (turn_driver.h:214-221) sets `woken_` (acq_rel), then
   under `loop_mutex_` posts a no-op to `loop_`. `drive()` (task-pin.hh:519-550) evaluates
   `done()` (`!turns.idle()`, blocking.h:98) unlocked at :535, then re-takes the scheduler mutex
   and checks `ready_` before `wait`/`wait_until` with the `queued` predicate (:544, :549);
   `push` (:561) appends and notifies under that mutex. A wake that lands after `done()` and
   before the wait leaves `ready_` non-empty, so the entry loops, runs the no-op, and its next
   `done()` sees the flag. A wake before the Hold sets only the flag, which the first `done()`
   reads. `woken_` is cleared at turn_driver.h:545 before the sealers are called, so a wake during
   a sealer call asks for the next turn. While the entry sleeps, `idle()` is true, so `woken_` is
   false and the `exchange` early return (:215) cannot swallow the post.
2. **Lost wake, `run()`.** Unchanged in effect: `wakeup_` is set in `Loop` before the loop's
   first `idle()` check and cleared after it exits (:316-326), so every wake while `run()` serves
   posts `woken`. The new windows between `Hold` and `Loop`, and between `~Loop` and `~Hold`,
   post the no-op, which touches no driver; the first `idle()` check reads the flag.
3. **Lifetime.** `loop_` is written only in `Hold`'s constructor (after the holder check, so a
   refused Hold leaves the holder's loop alone) and destructor, both under `loop_mutex_`, and read
   only in `wake()` under it. A post therefore completes before `~Hold` returns. The entry's Hold
   is a stack local of `run()` on the calling thread, declared first so destroyed last, and its
   scheduler is the caller's reference (or the overload's local, destroyed after the inner
   `run()` returns), so the scheduler outlives every post. A no-op left queued is dropped by
   `~blocking_scheduler` or run harmlessly by a later `drive()`. The entry's Hold cannot be
   destroyed on another thread. A `run()` frame destroyed after its loop is a lifetime the old
   `Loop`-held `loop_` had too: not introduced here.
4. **Where the waiter resumes; double resume.** The no-op runs on the entry's driving thread
   inside `drive()`; it resumes nothing. Waiters resume from `commit()` → `completed()` →
   `resume`, on the entry's thread, as before. One false-to-true transition of `woken_` posts once.
5. **Locks.** `loop_mutex_` now has the blocking scheduler's mutex under it, as it already had
   the asio post under `run()`. `drive()` never calls out under its mutex (`done()`/`may_wait()`
   and every queued call run unlocked), so no cycle.
6. **Doc.** turn-seal.md:222-228 matches the code in the present tense: the flag, the post to
   the holder's loop named by `Hold`, the no-op ending the entry's sleep, the next pass reading
   `idle()`, and the flag staying set after a hold ends. The `blocking.h` and `turn_driver.h`
   comments agree.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | consensus's blocking entry builds its own `TurnDriver::Hold(turns, Holder::entry)` with no loop, so under it a cross-thread `wake()` is still heard only at the next pass. The `loop = {}` default lets a holder stay deaf silently. | consensus/include/consensus/driver.h:1011; turn_driver.h:369 | Options: make `loop` required in `Hold` (greenfield; consensus then passes `scheduler_ref::of(scheduler)` at its pin bump), or keep the default and file a consensus follow-up quest. Owner's call. |
| N2 | no | nit | The test comment says a deaf entry "sleeps out the deadline and the round fails". In the fails-without run it woke at the wheel's next pass (about 8.2 s), every round succeeded, and only the latency check failed. So the `if (!woken) break;` never fires, and the mutant run sleeps all 100 rounds (819 s). | test/driver_test.cc:380, :414; job 173c61 log | Reword the comment, and break on the first round slower than 5 s, so a fails-without run costs one round. |
| N3 | no | nit | turn-seal.md:41 still calls `wake()`'s lock a leaf, while the member comment now says the loop's post is taken under it. | docs/mechanisms/turn-seal.md:41; turn_driver.h:693 | Say "takes only a lock no sealer call runs under, and the loop's post under it". |
| N4 | no | gap | `entry_wake_threads` reaches the wake-during-sleep interleaving by a 2 ms delay. It does not force the wake before the first pass, between `done()` and the wait, or after the work finishes. Reading settles those (item 1), but no test pins them. | test/driver_test.cc:378-419 | Optionally vary the writer's delay (0, a few us, and after `waiting`) over the rounds. |

## Not run

- No probe jobs. CI at 9e11713 was still running when I reviewed, and I do not rely on it.
- I did not check the consensus entry's behavior at runtime (N1 is from reading only), and I did
  not run any platform other than this macOS host.
