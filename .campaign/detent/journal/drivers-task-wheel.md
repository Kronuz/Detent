# drivers-task-wheel: journal

State: working
Quest: .campaign/detent/quests/drivers-task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Building the gaps (G1-G8 below) on quest/detent/drivers-task-wheel in task-wt.

## Next

1. G1 inline post_call, G2 blocking_scheduler, G3 tests, then jobs.

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
| 15 | `task::blocking_scheduler` (D5, X3): thread-safe `post`/`post_call` waking its wait, no wait without an armed timer; `blocking_entry threads` incl. `lost-wake-live-token` under TSan (R3) | absent | G2 `blocking_test` |
| 16 | D4's list of what moves onto the wheel and what stays (X7, Y7) | not in task's docs | G7 ARCHITECTURE.md "Timers on the wheel" |
| 17 | Xapiand lead reviews the diff | - | A5 message |

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | the gap list above |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

Pending.

## Rulings I made

- Row 1's `task::timer_wheel` is Kronuz/wheel (D72); its own tests (reentry, model, far, owner)
  stay in that repository, and task ports the bench cases through its schedulers.
