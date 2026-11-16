# drivers-task-wheel: journal

State: ready for review
Quest: .campaign/detent/quests/drivers-task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

msg-1 (Xapiand lead, A5 review of task:627aca3, CHANGES) folded in at task:a30bb0f; jobs queued
at it (Evidence). Correctness reviewer on 627aca3 runs in parallel; its findings go on top.

## Next

1. Read the jobs at task:a30bb0f; fold in the correctness review and the Xapiand lead's re-check.

## Messages

- msg-1 (xapiand-lead, CHANGES): fixed in task:a30bb0f.
  - X1: `drive(done, may_wait)` sleeps with no deadline when nothing is armed and `may_wait()`
    holds; `drive(done)` defaults `may_wait` to "a timer the clock reaches is armed", so the
    lost-wake cases are unchanged. Xapiand's `BlockingScheduler::drive(done)` is
    `drive([&]{ return done; }, []{ return true; })` inside its `HostScope`; ARCHITECTURE.md
    "Schedulers" says so, with D5's entry passing "an operation deadline is armed". Test
    `blocking_test wakes-unarmed` (post and post_call at 20 ms; 5,000 rounds with the post around
    the decision to sleep, a 10 s per-round watchdog); mutant `unarmed-returns` kills it.
  - X4: `drop_timer` is static; inline and blocking read the scheduler only while the entry is
    armed (their destruction fires every entry off). Test `late`; mutant
    `drop-calls-dead-scheduler` (the old call, which with X7's assertion reads the freed
    scheduler) is killed by ASan.
  - X5: asio `arm` asserts the loop (no closed allowance); `cancel` keeps it. Control
    `deadline_asio_test closed-arm` aborts; mutant `closed-arm-allowed` makes it exit 0.
  - X7: `blocking_scheduler::arm`/`cancel` assert `runs_here()`. Control `offthread-cancel`
    (cancel of an unarmed entry off-thread, which the wheel's check never sees); mutant
    `blocking-cancel-unchecked`.
  - X8: fixed, small: each `drive` pass runs what was queued when it began, then expires the
    wheel (one `now()` per pass). Test `starve` (a self-reposting call, a 20 ms timer fires at
    20.0 ms); mutant `expire-when-idle` holds it off past 2 s. Xapiand's `post_demoted` maps to
    `post`, noted in ARCHITECTURE.md.

## Gap list (A1): design row 1 against task main (task:f51aafb, wheel:bfd72bc)

Row 1 names `task::timer_wheel`; D72 replaces it with Kronuz/wheel, which task already fetches at
wheel:bfd72bc and uses (`task.hh:57`, CMakeLists.txt). Clauses in row order:

| # | Clause (design row 1) | On task main | Gap |
|---|---|---|---|
| 1 | the wheel (D4): O(1) arm and cancel, no allocation (job) | Kronuz/wheel `wheel_alloc_test`, bench; task `deadline_alloc_test` (inline_scheduler, 10,000 deadlines, 0 allocations) | asio_scheduler not counted: G6 |
| 2 | `expire` tolerates a fire cancelling or re-arming any entry, itself included (`reentry`, `chain`, review probe as tests) | Kronuz/wheel `wheel_test reentry`; task `deadline_test chain` | through task's scheduler (`inline_scheduler::advance`, `scheduler_ref::arm/cancel`): G3 `deadline_test reentry` |
| 3 | matches a reference model under random arm, cancel and re-arm from fires | Kronuz/wheel `wheel_test model` | through task's scheduler: G3 `deadline_test model`; mutant firing a re-armed entry early: G3 |
| 4 | deadlines beyond a round never fire early, `floor(d / round)` early wakes | Kronuz/wheel `wheel_test far` (hierarchical since D74: at most one early wake per level, not per round) | through `advance`: G3 `deadline_test long` (never early, at most a tick late) |
| 5 | owner-thread Debug assertions, bound when the loop starts | Kronuz/wheel binds on the first owner call, `wheel_test owner` (aborts); asio `arm`/`cancel` assert `runs_here()` | no task test: G8 `deadline_asio_test offloop-arm` (must abort) |
| 6 | `deadline<S>` on `S::clock`; `inline_scheduler::advance` expires it with no real time | `detail::clock_of`, `inline_scheduler::clock`; `deadline_test manual` (5 s simulated, real < 1 s) | none |
| 7 | a parent and children on two loops under TSan and ASan | `deadline_test chain` is one scheduler | G5 `deadline_asio_test loops` (bench `wheel_test loops` on two asio_schedulers) |
| 8 | `asio_scheduler::start_timer` on the wheel, contract unchanged: `asio_test.cc:371-390` as is, plus `wheel_test offloop` | `task_asio.hh` start_timer/drop_timer; `asio_test` (the 300-round any-thread case); `deadline_asio_test offloop` | none |
| 9 | the entry in the deadline's cancel state, no added allocation | `detail::timed_cancel_state` (`task.hh:1067`) | asio count: G6 |
| 10 | a wheel destroyed with deadlines armed releases them | `deadline_asio_test teardown` (wheel drops three, tokens unrequested) | none |
| 11 | `close()` releases the wheel; phase-3 `destroy_roots` of frames holding wheel deadlines, a group's entry destroyed after, clean under ASan and TSan (Y1) | `close()` calls `wheel.release()`; teardown test has no roots and no group entry; asio `cancel` asserts `runs_here()`, so a group's entry cannot unlink on teardown's thread | G4: `cancel`/`arm` allowed once closed; `deadline_asio_test roots` |
| 12 | `cancellation.md` and the D4 users table unchanged | no caller changes | G7: check, keep current |
| 13 | `scheduler_ref` owner-thread `arm` and `cancel`, the wheel private (X14, D79) | `task.hh:197-240`; `deadline_test ref`, `deadline_asio_test direct` | none |
| 14 | `inline_scheduler` same-thread `post_call` | absent | G1 `deadline_test post_call` |
| 15 | `task::blocking_scheduler` (D5, X3): thread-safe `post`/`post_call` waking its wait, no wait without an armed timer; `blocking_entry threads` incl. `lost-wake-live-token` under TSan (R3) | absent | G2 `blocking_test`; "no wait without an armed timer" is `drive(done)`'s default, `may_wait` widens it (msg-1 X1, task:a30bb0f) |
| 16 | D4's list of what moves onto the wheel and what stays (X7, Y7) | not in task's docs | G7 ARCHITECTURE.md "Timers on the wheel" |
| 17 | Xapiand lead reviews the diff | - | A5 message |

## Blocked on

Nothing.

## Evidence

At task:a30bb0f (queued 2026-11-15 23:09-23:10):

| Criterion | Evidence |
|---|---|
| A1 | the gap list above (G1-G8 built in task:627aca3; row 15 amended by task:a30bb0f) |
| A2 | suite 261115-230919-11321d; mutant `rearm-fires-early` 261115-231029-7e7232 (fails without) |
| A3 | ASan+UBSan 261115-230928-237422; TSan 261115-230935-8e4110; controls `offloop-arm` 261115-230958-6c0bcc, `closed-arm` 261115-231005-ddbf38; mutant `closed-arm-allowed` 261115-231058-da579c (fails without) |
| A4 | TSan 261115-230935-8e4110; X1 `wakes-unarmed` under TSan 261115-230951-3981e7; control `offthread-cancel` 261115-231014-093c6c; mutants (fails without) `unarmed-returns` 261115-231021-fa893c, `expire-when-idle` 261115-231036-5eaf16, `drop-calls-dead-scheduler` 261115-231043-6bb6ac, `blocking-cancel-unchecked` 261115-231051-65b2a4 |
| A5 | [artifact] task `ARCHITECTURE.md` "Schedulers" (Xapiand's BlockingScheduler and D5's entry on `drive`) and "Timers on the wheel"; `docs/mechanisms/cancellation.md`, `deadlines.md` current. Xapiand lead: msg-1 CHANGES on task:627aca3 (X1 blocking), fixed in task:a30bb0f, re-check asked in msg-48 |

At task:627aca3 (superseded): suite 261115-224042-c56fa6, ASan+UBSan 261115-224045-7cef95, TSan
261115-224052-f3ced1, mutant 261115-224059-7bf17d, control 261115-224121-def07d.

## Result

task:627aca3 on quest/detent/drivers-task-wheel: `task::blocking_scheduler` (task.hh), inline
`post_call`, asio arm/cancel on teardown's thread once closed; tests listed in task's
`docs/mechanisms/deadlines.md` "Tests"; D4's list in task `ARCHITECTURE.md` "Timers on the
wheel". Measured locally (Debug, macOS x86-64): 0 allocations per deadline on inline, blocking
and asio (10,000 each); blocking cross-thread cancel returns 54-57 ms after a 50 ms request,
200 ms deadline at 206-208 ms.

## Rulings I made

- Row 1's `task::timer_wheel` is Kronuz/wheel (D72); its own tests (reentry, model, far, owner)
  stay in that repository, and task ports the bench cases through its schedulers.
- inline_scheduler's ready queue holds `{fn, arg}` calls (16 B) with resumes as one such call,
  so `post` and `post_call` share one FIFO; demoted stays handles. One queue shape, also
  blocking_scheduler's.
- blocking_scheduler: `drive(done, may_wait)` sleeps while `may_wait()` holds, toward the
  wheel's `next()` or with nothing armed until a post (msg-1 X1); `drive(done)`'s default is "a
  timer the clock reaches is armed" (`next() != never`, not `size() != 0`, so an entry at the
  wheel's end keeps today's lost wake). D5's entry passes "an operation deadline is armed". It
  returns bool; `stalled` stays journal's.
- X8: drive() runs a pass of what was queued when it began, then expires; costs one clock read
  per pass while busy, none when the queue empties as before.
- blocking_scheduler deadlines and arm/cancel are on the driving thread (as inline), not any
  thread (as asio): its frames run there; the wheel's owner check asserts it. Destroyed, it
  releases the wheel so the destroying thread drops what is armed.
- asio after close: deadlines still leave their entries for the wheel's drop (no in-place
  unfile), since a deadline may die on any thread; only `cancel` (a driver's entry, teardown's
  thread) runs in place: `cancel` asserts `!io_ || runs_here() || closed()`, `arm`
  `!io_ || runs_here()` (msg-1 X5).
- `drop_timer` is a static `S::drop_timer(S*, state*)` on all three schedulers (one shape);
  asio's still needs its scheduler alive, as documented.
- Assertion controls (`offloop-arm`, `closed-arm`, `offthread-cancel`) exit 0 when no assertion
  fires, so `! case` fails without it; `offloop-arm` returned 1, so its `!` job passed either way.
- `test/wheel_mutant.py` is the one mutant runner for timers: wheel.hh or task's headers, a
  per-mutant kill code (1 a CHECK or ASan report with `abort_on_error=0`, 0 a control finding no
  assertion), ASan flags per mutant. Exit 0 when killed, so its jobs are `--fails-without`
  without `!` (lead asked for `!`: the script's exit code is the same contract).
- Row 1's `floor(d / round)` early wakes is superseded by the hierarchical wheel (D74): at most
  one early wake per level, Kronuz/wheel `wheel_test far`; task checks never-early via advance.
- The mutant script follows `test/lanes_mutant.py`'s contract (exit 0 when killed), so its job
  is `--fails-without` without a `!`.
