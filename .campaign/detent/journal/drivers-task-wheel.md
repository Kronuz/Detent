# drivers-task-wheel: journal

State: ready for review
Quest: .campaign/detent/quests/drivers-task-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs queued at task:627aca3: suite 261115-224042-c56fa6, ASan+UBSan 261115-224045-7cef95, TSan
261115-224052-f3ced1, mutant 261115-224059-7bf17d, offloop-arm control 261115-224121-def07d
(after the suite). A5: msg-47 to the Xapiand lead with the diff, reply pending.

Local Debug, before the jobs: every ctest green except one `task_asio` abort under `ctest -j2`
(output not kept); 12 serial and 15 three-way-parallel reruns of task_asio_test all passed. If
a job reproduces it, rerun the same at main's commit to attribute it.

## Next

1. Read the jobs; fold the Xapiand lead's reply (msg-47) into A5; launch-ready for review.

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
| A1 | the gap list above (G1-G8 built in task:627aca3) |
| A2 | suite 261115-224042-c56fa6 (`deadline_test manual`, `reentry`, `model`, `long`, `post_call`); mutant 261115-224059-7bf17d (`test/wheel_mutant.py rearm-fires-early`, killed by `reentry` and `model`; locally both exited 1) |
| A3 | ASan+UBSan 261115-224045-7cef95 and TSan 261115-224052-f3ced1 (`asio_test` unchanged, `deadline_asio_test offloop`, `teardown`, `roots`, `loops`; `deadline_alloc_test` asio 0 allocations); control 261115-224121-def07d (`offloop-arm` aborts) |
| A4 | TSan 261115-224052-f3ced1 (`blocking_test` cancel-thread, deadline-steady, lost-wake, lost-wake-live-token, rounds, wakes) |
| A5 | [artifact] task `ARCHITECTURE.md` "Timers on the wheel"; `docs/mechanisms/cancellation.md`, `deadlines.md` current; Xapiand lead review: msg-47 sent, reply pending |

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
- blocking_scheduler: `drive(done, may_wait = always)`. The scheduler never sleeps without an
  armed timer; `may_wait` lets the journal/consensus entry count only operation deadlines
  (D5 step 4) without task knowing group timers. It returns bool; `stalled` stays journal's.
- blocking_scheduler deadlines and arm/cancel are on the driving thread (as inline), not any
  thread (as asio): its frames run there; the wheel's owner check asserts it. Destroyed, it
  releases the wheel so the destroying thread drops what is armed.
- asio after close: deadlines still leave their entries for the wheel's drop (no in-place
  unfile), since a deadline may die on any thread; only `arm`/`cancel` (a driver's entries,
  teardown's thread) run in place. Assertion is `!io_ || runs_here() || closed()`.
- Row 1's `floor(d / round)` early wakes is superseded by the hierarchical wheel (D74): at most
  one early wake per level, Kronuz/wheel `wheel_test far`; task checks never-early via advance.
- The mutant script follows `test/lanes_mutant.py`'s contract (exit 0 when killed), so its job
  is `--fails-without` without a `!`.
