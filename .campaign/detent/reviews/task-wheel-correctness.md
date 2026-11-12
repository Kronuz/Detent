# task-wheel review: correctness

Commit: task:c0cda2371a54a033c1960324deb474d3fcb4d28a
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261112-080317-77f500 (Debug suite at task:c0cda23): `deadline_test` (manual, chain, ref),
  `deadline_alloc_test`, `deadline_asio_test` (offloop, teardown, idle), `asio_test.cc` unchanged.
- 261112-080533-082edf (A3 mutant, fails without), 261112-080331-647f35 and 261112-080334-3404ab
  (TSan, ASan+UBSan of the suite; A4), 261112-080531-e445ce (A1 grep). I did not rerun any of them.
- Kronuz/wheel at wheel:bfd72bc: `wheel.hh` and `docs/timer-wheel.md` read in full for the four rules.

## What I checked myself

Probe job 261112-082613-74dd5c at task:c0cda23, Debug and ASan+UBSan, exit 0. Scripts:
`.scratch/review-task-wheel-correctness/{run.sh,CMakeLists.txt,probe_inline.cc,probe_asio.cc}`;
rerun with `campaign job submit --quest task-wheel -- sh <abs>/run.sh`.

1. **Owner-thread cancels (risk 1).** Read: every wheel call goes through `file`/`unfile`, which
   run only when `runs_here()` or from a posted handler; a linked parent's request off the loop
   touches only the cancel state, never the wheel. Exact for an io_context executor. See N2 for
   other executors.
2. **Teardown (risk 2).** A8: 50 rounds of coroutines awaiting armed deadlines (one 60 s, one
   due around the stop), then stop, join, `close`, `destroy_roots` and destroy. Clean under
   ASan, `live_frames` back to baseline, no completion after teardown. The wheel's destructor
   fires each entry `false` once (`wheel.hh` ~timer_wheel), and `on_wheel(false)` only releases.
   Teardown destroys the awaiting frames, so they never resume and no reason is set, which is correct.
3. **Fire and cancel in one turn (risk 3).** I1: two deadlines in one slot, where the first's
   callback destroys the second, in both orders. I2: a parent request, then the fire. I3: a
   callback destroys its own deadline during the fire. I4: destroyed before its time. Each
   resolves at most once, with the first reason kept. The wheel unlinks a due entry before its
   fire, and `disarm` precedes `drop_timer`.
4. **The asio wake (risk 4).** A1: a later deadline, then an earlier one on the loop and one off
   it. A2: a new deadline after the wheel emptied by expiry. A3: a new deadline after an unfile
   emptied it and cancelled the wake. A4: a deadline already past. A5: 4.6 s (upper level,
   cascade). Each fires once, never early, and 0.5 to 2.2 ms late. A6: `time_point::max()` does
   not spin (0.1 ms CPU in 300 ms). The re-arm logic is sound by reading too: a stale successful
   wait runs `on_wake`, which recomputes `next()`. It cannot sleep past a due deadline filed
   through `start_timer`. The exception is F1.
5. **Removed tests (risk 5).** None is removed relative to main: the wheel tests were added on
   this branch. Of the task:5a1e516 tests, `reentry`, `model`, `far`, `cross`, `owner` and the
   wheel half of the alloc test are wheel-only and live in wheel:bfd72bc `test/`. `manual`,
   `chain`, `ref`, `offloop`, `teardown`, `idle` and the deadline alloc case stay.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | An entry armed directly on `asio_scheduler::wheel()` (the wheel `scheduler_ref::wheel()` hands out for X14's Driver timers) never wakes the loop. Only `file()` calls `wake_by`, so the entry is stranded until some `task::deadline` happens to wake the loop, and then it rides along. This contradicts the accessor's own comment ("one asio::steady_timer wakes the loop at the wheel's next()"). | Probe A7, job 261112-082613-74dd5c: fired_after_200ms=0 for a 20 ms entry; fired once a 5 ms deadline woke the loop. `task_asio.hh:132-134`, `task.hh:202` | Owner's choice, any of: (a) an owner-thread `arm_timer(entry, at)` on the scheduler (or a `rewake()` after arms) that calls `wake_by`, with scheduler_ref forwarding it; (b) have `on_wake`'s caller re-arm after each loop pass; (c) state in `wheel()`'s comment and API.md that arms on it need a wake, and leave X14 to the scheduler-on-wheel quest. Test: probe A7 as a `deadline_asio_test` case where an entry armed through `scheduler_ref::wheel()`, with no deadline armed, fires within a bound. |
| N1 | no | nit | deadlines.md:58 says a deadline requests "at most 1 ms after its time". On asio the bound is the slot end plus the loop's wake latency: 2.0 to 2.2 ms in Debug, 0.5 to 1.5 ms under ASan. | Probe A1-A5 late_us | Say "at most one 1 ms tick past its time, plus the loop's wake latency". |
| N2 | no | gap | `runs_here()` is false for any executor other than `io_context::executor_type` (`task_asio.hh:102`), so every filing is posted. With a strand over a multi-threaded io_context the handlers change threads, and the wheel's Debug owner assert fires. A per-deadline `steady_timer` did not care. No in-tree user passes a strand (grep across Kronuz repos). | Reading `task_asio.hh:102,170,200`; not probed | Document that asio_scheduler needs a single-threaded io_context executor, or keep it as is. |
| N3 | no | nit | deadlines.md:59 says "created and destroyed on any thread", but `inline_scheduler`'s `drop_timer` and `start_timer` call the wheel in place, so off its thread they hit the owner assert. | Reading `task.hh` inline_scheduler::start_timer/drop_timer | Scope "any thread" to asio_scheduler. |

## Not run

- No rebuild or rerun of the suite, TSan, ASan or the mutant: the jobs above cover them.
- No lateness baseline on main's per-deadline `steady_timer`, so N1 does not say whether lateness grew.
- No strand or thread_pool executor probe (N2 is from reading the code).
- Linux and FreeBSD were not probed locally; CI covers them.
