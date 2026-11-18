# task-wheel review: correctness

Commit: task:9a1bb31
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 2. Round 1 reviewed task:c0cda23 and raised F1. This round reviews task:c0cda23..9a1bb31
(D79: the scheduler's owner-thread `arm`/`cancel`; `scheduler_ref` forwards `now`, `arm` and
`cancel`; `wheel()` removed), against the quest text at Detent:8b6cd792.

## Evidence I relied on

- Round 1: the suite, TSan, ASan+UBSan, mutant and grep jobs at task:c0cda23, and my probe job
  261112-082613-74dd5c. They covered teardown, exactly-once resolution, the asio wake through
  `start_timer`, and the removed tests. `file`/`unfile` now route through `arm`/`cancel`, with
  the same wake logic as before, so those conclusions carry over.
- 9a1bb31's new tests read: `deadline_asio_test direct` and `deadline_test ref`. I did not rerun
  the suite at 9a1bb31.

## What I checked myself

Scripts are in `.scratch/review-task-wheel-correctness/`: `probe_arm.cc`, `run_arm.sh`, and a
`BUILD_PROBE_ARM` block in `CMakeLists.txt`. The probe arms through `scheduler_ref::arm` where it
exists and through `wheel().arm` at c0cda23, so the same source builds at both commits.

- F1 confirmation, `run_arm.sh wake`:
  - 261112-084517-78b633 at c0cda23 fails (count=0).
  - 261112-084942-3e4b76 at 9a1bb31 passes. The arm fired 1.6 ms late.
- Q1: can a cancel that empties the wheel race a pending wake? No. The loop is single-threaded.
  If a stale successful wait runs after `cancel` (and after a new `arm`), `on_wake` recomputes
  `next()` and re-arms the wake, cancelling the newer wait. 300 rounds of "cancel the only entry
  near its wake, then arm anew": every new arm fired once, on time (same job).
- Q2: can an arm earlier than the current wake still be missed? No, for `at` within the clock's
  range. `wake_by` keeps the minimum. Probes: an earlier arm fired 2.8 ms late, and an arm from a
  fire for a time already due fired 24 us late (same job). F2 is the exception.
- Q3: is anything left that arms behind the scheduler's back? No. `git grep` at 9a1bb31 finds
  `wheel()` private on inline_scheduler and `timers_` private on asio. Every `arm`/`cancel` goes
  through the scheduler. task never calls `arm_remote`.
- `run_arm.sh far` at 9a1bb31, job 261112-085006-0064f9, fails. This is F2.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Fixed at 9a1bb31. An entry armed through the scheduler wakes the loop, and `wheel()` is gone. | Fails at c0cda23: 261112-084517-78b633. Passes at 9a1bb31: 261112-084942-3e4b76. Test: `deadline_asio_test direct`. | none |
| F2 | yes | defect | `asio_scheduler::arm(e, at)` with `at` past INT64_MAX ns spins the loop on a full core. Example: `wheel::timer_wheel::never`, which consensus Core uses as its saturated "never" deadline (`consensus/core.h:281`, `maximum = UINT64_MAX`), the deadlines X14's Driver will arm. `arm` computes `(at / tick + 1) * tick`, which wraps for `never`. `wake_by` then builds `time_point(nanoseconds(at))` from a uint64 past INT64_MAX, which turns negative. The wait completes at once, `expire` finds nothing, `next()` returns the same far value, and the loop repeats. `start_timer` deadlines are capped by `steady_clock`, so only direct arms hit this. | `run_arm.sh far`, job 261112-085006-0064f9: arm at `never` used 308.5 ms CPU in 300 ms, fired 0 times. Code: `task_asio.hh:142`, `task_asio.hh:218`. | In `wake_by`, skip the wake (or clamp it to `clock::time_point::max()`) when `at` exceeds the clock's range; compute the slot end in `arm` without overflow. Test: the `far` mode as a `deadline_asio_test` case (arms at `never` and at 2^63 + 1 ms, CPU bounded, nothing fires). |
| N1 | no | nit | Round 1's lateness nit is resolved: `arm`'s comment and the docs now say "one tick plus the loop's wake latency". | `task_asio.hh` arm comment | none |
| N2 | no | gap | Resolved as documentation: asio_scheduler's class comment now requires a single-threaded executor. | `task_asio.hh:50-55` | none |
| N3 | no | nit | Not rechecked. Round 1: "created and destroyed on any thread" holds for asio_scheduler, not inline_scheduler. | round 1 | Scope the sentence. |
| N4 | no | nit | `inline_scheduler::cancel` and `asio_scheduler::cancel` dereference the wheel when `e.armed()` but this scheduler never armed anything. That is an entry armed on another scheduler, a caller error; it crashes rather than asserting. | `task.hh` inline_scheduler::cancel; `task_asio.hh` cancel (`*timers_`) | Optional Debug assert. |

## Not run

- The suite, TSan and ASan at 9a1bb31: those belong to the contributor's jobs, and I did not check them.
- Probes under ASan or TSan this round (Debug only).
- Linux and FreeBSD, which CI covers.
