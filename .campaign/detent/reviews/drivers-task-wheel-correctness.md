# drivers-task-wheel review: correctness

Commit: task:627aca39d131dfaf9ffd9cdf9c303bf1febb3df8
Verdict: CHANGES
Date: 2026-11-15

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Lens: correctness, concurrency included. Reviewed task:f51aafb..627aca3 against the quest file,
D72/D96/D97, async-drivers D4, D5 and Build plan row 1, and the journal at Detent-wt:f60457a1.
The branch has since moved to task:a30bb0f (another review's fixes, X1-X8); I did not review
it, but F1's probes still fail there (261115-232148-0d1ee4).

## Evidence I relied on

- Suite 261115-224042-c56fa6 (Debug, 9/9), ASan+UBSan 261115-224045-7cef95 and TSan
  261115-224052-f3ced1 at 627aca3: the new cases run and pass; I did not rerun them.
- Mutant 261115-224059-7bf17d: `rearm-fires-early` fails `deadline_test reentry` and `model`
  (log read: both exit 1 on their CHECKs). A2's fails-without holds.
- Control 261115-224121-def07d: its log shows the owner assertion aborting `offloop-arm`. The
  job's exit code alone does not show it (F5).

## What I checked myself

Scripts under `Detent/.scratch/review-drivers-task-wheel-correctness/` (`probes.cc`,
`run_probes.sh FLAVOR [CASE...]`, `stress_asio.sh FLAVOR N MAXSEC`, `mutant.py NAME FLAVOR`,
which exits 0 when the mutant is killed).

1. Q1, blocking_scheduler. Probes at 627aca3, plain/ASan/TSan (261115-225518-78a854,
   261115-225522-2f459a, 261115-225526-786650; race reruns 261115-225844-e17f7b plain,
   261115-225852-8f2dc5 TSan):
   - `race` (a cross-thread cancel at 0-3 ms against a 0.5-3 ms deadline, 2,000 rounds):
     drive() returned false (a stall) in 40 rounds plain and 31 under TSan; in 39 and 31 of them
     the cancel had claimed the waiter but not yet posted its hop, and the deadline had fired. F1.
   - `race-forced` (the same interleaving, forced by holding the callback between claim and hop):
     stalls deterministically, 8.7 ms plain, 7.9 ms TSan. F1.
   - `window` (3,000 random posts around drive's start, far timer armed): no lost wake, slowest
     0.36 ms. `window-forced` (the post made from inside may_wait(), between the queue check and
     the sleep): runs at once, 0.2 ms plain, 10.9 ms TSan (261115-230341-7ca38f). The lock
     discipline is right: post signals under the mutex, and the post-expire recheck plus the
     wait predicate cover the window.
   - `destroy` (scheduler destroyed as soon as drive() returns, the poster still unwinding):
     clean under ASan, 3,000 rounds.
   - Sleeping with no armed timer: the code returns false when `next() == never`; mutant
     `blocking-sleep-unarmed` is killed by `lost-wake` (ctest timeout).
   - Deadlines on the driving thread only: enforced only by the wheel's Debug owner check at
     627aca3 (first owner call binds); documented in API.md and deadlines.md.
2. Q2, asio after close. `asio-close` probe, 100 rounds, plain/ASan/TSan (same three jobs): after
   stop, join and close(), teardown's thread arms a new entry, cancels one, cancels and re-arms a
   group entry, and makes a deadline after close; destroy_roots() destroys three frames holding
   deadlines; the destroyed wheel fires `false` exactly once for each entry still filed (2), never
   for the cancelled one; no token is requested; every frame and cancel state is freed. Reading
   task_asio.hh at 627aca3: every handler that could touch the wheel (`wake_by`'s wait,
   start_timer's and drop_timer's posted filings) checks `closed()` first, and `closed()` is an
   acquire load of the release store in close(); with phase 1 (join) before phase 2 the io thread
   cannot touch a closed wheel. Release is exactly once: an entry leaves through its fire, an
   `unfile` (only on the loop) or the wheel's destructor, never two. Note N1 for the violated-order
   case.
3. Q3, the `task_asio` abort. 200 runs each, two at once as ctest -j2, Debug at 627aca3: plain
   0 failures (261115-225529-9f9ea4, 334 s), ASan+UBSan 0 (261115-225532-31d827, 594 s), TSan
   12 failures (261115-225535-863f7d): 8 at `asio_test.cc:758` `CHECK(st.inlined > 0)`, a
   timing-dependent coverage check that fails the run, and 5 at `asio_test.cc:1700`
   `CHECK(aborts(cancel_without_scheduler))`, a fork death test whose child starts threads. No
   abort and no sanitizer report. Neither line is in this diff (asio_test.cc and the code they
   exercise, `await_asio` and sync_wait, are unchanged). Attribution at task:f51aafb:
   261115-231813-10e137, queued at writing (Not run). The contributor's plain-build abort did not reproduce.
4. Q4, A1's gap list: I read each of row 1's clauses against task:f51aafb and the diff. Every
   clause is mapped, either to a test on main that exists there (`deadline_test manual`, `chain`,
   `ref`; `deadline_alloc_test`; `deadline_asio_test offloop`, `teardown`, `direct`; asio_test's
   any-thread case) or to G1-G8 in this commit. Two clauses are met in substance, not to the
   letter, and the journal's rulings say so: "`floor(d / round)` early wakes" (superseded by the
   hierarchical wheel, D74; task checks never-early only) and "bound when the loop starts" (the
   wheel binds on its first owner call, which on asio_scheduler is asserted to be on the loop).
   Design D4 also says a deadline destroyed on teardown's thread after close unlinks in place;
   627aca3 leaves it filed for the wheel's destructor (ruling 5). That is safe and frees the
   state one phase later. No clause is missing.
5. Q5, mutants, each run as a job at 627aca3:

   | Mutant | Job | Result |
   |---|---|---|
   | `blocking-no-recheck`: no queue recheck after the fires | 261115-225546-3a19e5 | killed (`task_blocking`) |
   | `blocking-sleep-unarmed`: sleeps with nothing armed | 261115-225549-080193 | killed (`lost-wake` hangs) |
   | `blocking-ignore-may-wait` | 261115-225552-b9ad35 | killed (`wakes`) |
   | `blocking-push-unlocked` (TSan) | 261115-225600-a5bef9 | killed (TSan race on the deque) |
   | `blocking-push-unlocked` (plain) | 261115-225603-500d55 | survives; TSan is the gate |
   | `blocking-cancel-keeps`: cancel leaves the entry filed | 261115-225611-47cca2 | killed (`task_blocking`, `task_deadline_alloc`) |
   | `asio-cancel-keeps`: cancel leaves the entry filed | 261115-225616-4770a4 | killed (three tests) |
   | `asio-closed-revert`: G4's `closed()` clause removed | 261115-225620-6a5386 | killed (`roots` aborts): G4's fails-without |
   | `asio-close-keeps-owner`: close() does not release the wheel | 261115-225625-5e0257 | killed (`roots`, owner assertion) |
   | `blocking-lost-wake`: sleeps when a timer is armed, without rechecking the queue | 261115-225543-11f479, 261115-230348-095f7b | survives, `window-forced` included, but it is equivalent (the wait predicate kept); F2 |
   | `blocking-return-early`: returns once done() holds, work still queued | 261115-225556-f76801 | survives; F3 |
   | `blocking-notify-after-unlock` (ASan) | 261115-225607-77f485 | survives, `destroy` included; N2 |

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | `blocking_scheduler::drive` reports a lost wake when a cross-thread cancel is in flight as the operation deadline fires. The other thread wins the timed state's request, and before its callback posts the hop, the driver's expire fires the deadline (its request fails, already requested, `task.hh:1186`), the wheel empties, and drive returns false (`task.hh:532`, `:536`). D5 (and row 4: "a cross-thread cancel toward a deadline returns `cancelled`") wants `cancelled`; the blocking entry would throw `stalled`, a `logic_error` Xapiand reports as a 5xx and never retries. 2% of rounds in the race probe. The bench's `blocking.hh` has the same shape, so the design inherits it. | probes `race` 40/2000 plain, 31/2000 TSan; `race-forced` stalls every run (261115-225844-e17f7b, 261115-225852-8f2dc5). Still present at task:a30bb0f: `race` 33/2000, `race-forced` stalls at 8.4 ms (261115-232148-0d1ee4) | The fix belongs to the contributor. Options: (a) a fire whose request loses to a request still running callbacks on another thread waits for that request to finish (cancel_state already tracks `running_` and `epoch_`), so the hop is queued before expire returns; (b) the deadline's fire holds the driver awake, for example a pending count the fire raises and the hop lowers. Test: `race-forced` as a `blocking_test` case (the callback held between claim and hop while the deadline fires, drive returns cancelled), plus the random race rounds under TSan. |
| F2 | no | gap | No suite case forces a post between drive's queue check and its sleep. Two guards cover that window: the recheck after the fires (`task.hh:534`) and the wait predicate (`:539`). My `blocking-lost-wake` mutant weakened only the recheck and kept the predicate, so it was equivalent. It survived the suite, `window` and `window-forced` (261115-225543-11f479, 261115-230348-095f7b), which proves nothing about a gap. `window-forced` passes at head (261115-230341-7ca38f). I did not run a mutant that removes both guards while a timer stays armed. | as stated | Optional: add `window-forced` to `blocking_test` (may_wait() posts from another thread on its first call, a 10 s timer armed, drive returns at once). |
| F3 | no | gap | Nothing tests that drive runs the queue to empty before returning true (D5 steps 1-3): mutant `blocking-return-early` survives. Journal-driver's `blocking_entry rules` (staged-then-cancelled written before return) will cover the entry; a scheduler-level case is cheap. | 261115-225556-f76801 | A `blocking_test` case where done() turns true while a later post is queued: drive runs it before returning. |
| F4 | no | gap | `asio_test.cc:758` `CHECK(st.inlined > 0)` is a timing-dependent coverage check that fails the run (the proof standard says such a check reports and never fails), and `asio_test.cc:1700`'s fork death test is flaky under TSan. Together they are 12 of 200 TSan runs. Probably what the contributor saw. Not introduced here if the f51aafb run shows the same. | 261115-225535-863f7d, attribution 261115-231813-10e137 | Outside this quest: report the line-758 count instead of failing on it; run `aborts(...)` cases only in non-TSan builds. |
| F5 | no | nit | The `offloop-arm` control returns 1 when no assertion fires (`deadline_asio_test.cc:311`), so `! task_deadline_asio_test offloop-arm` passes either way, Release builds included. The log shows the abort today. (a30bb0f's message says its controls now exit 0 without their assertion.) | 261115-224121-def07d; `deadline_asio_test.cc:297-311` | Return 0 when no assertion fires. |

Non-blocking notes:

- N1. close() asserts only `!runs_here()`, not that the loop is joined. Before this diff, a
  teardown-thread cancel with the loop still running aborted in Debug; with `|| closed()` it now
  races on the wheel silently. That only happens when phase 1 is violated.
- N2. "Signals under the lock so the scheduler may be destroyed after the unlock" is untested:
  `blocking-notify-after-unlock` survives ASan and the `destroy` probe. The rule is right; a
  test would have to land in a window of a few nanoseconds.
- N3. A frame that yields forever under a deadline never sees it fire on blocking_scheduler at
  627aca3 (`starve`: deadline_fired_while_yielding=0), because drive expires the wheel only on an
  empty queue. That matches D5 step 1 as written. a30bb0f changes it (X8).
- N4. `docs/mechanisms/cancellation.md:64-66` still says a deadline "may be destroyed on any
  thread"; API.md and deadlines.md restrict inline and blocking to their own thread.

## Not run

- 261115-231813-10e137 (TSan `task_asio_test` x200 at task:f51aafb, F4's attribution) was still
  queued when I wrote this; the lead reads it.
- A lost-wake mutant that removes both guards (F2).
- Anything else at task:a30bb0f; its fixes need their own review.
- Linux, g++ and the CI matrix; Release (NDEBUG) builds.
- A5's Xapiand reply (msg-47) and the docs review beyond the thread rules.
- `deadline_asio_test` and `lanes_test` stress: only `task_asio_test` was repeated.
