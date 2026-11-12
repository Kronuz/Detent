# task-wheel: task::timer_wheel, the one Kronuz wheel

Risk: high (concurrency, a shared public API). The lead writes this quest; contributors read it and
never edit it. Machine state: `campaign status task-wheel`.

## Outcome

Kronuz/task ships `task::timer_wheel`, candidate (f) of `docs/design/wheel-evidence.md` (D70, D71): a
single-owner hierarchical wheel with a 64-stripe intrusive inbox for arms from other threads, 32-byte
intrusive entries, no heap per arm. Its re-arm costs what the prototype's (a) does, so the one wheel
pays nothing on the single loop. `task::deadline`, `inline_scheduler` and `asio_scheduler` run on it
(async-drivers quest 1, `docs/design/async-drivers.md` table row 1).

## Acceptance criteria

- A1 Re-arm gap closed (D70): on macOS, an `exclusive` A/B on the wheel-evidence harness
  (`docs/design/async-drivers-bench/wheel_vs_stash.sh`, A5's >= 100 ms samples, R = 10) puts the
  task wheel's single-loop score within 1.10x of (a) at n = 1,000 and n = 1,000,000, and its
  cross-thread score at P = 1, 4, 14 within 1.10x of (f)'s rows; the A/A band is reported.
- A2 Correctness: the wheel matches a reference model under random arm, cancel and re-arm,
  including re-arm and cancel from inside a fire (itself included); deadlines beyond a round never
  fire early; cross-thread arms of unarmed entries from 1, 4 and 14 threads all fire once; owner-only
  cancel and re-arm asserted in Debug: tests in the suite.
- A3 Integration per async-drivers row 1: `deadline<S>` on `S::clock`; `inline_scheduler::advance`
  expires with no real time; `asio_scheduler::start_timer` on the wheel with the deadline contract
  unchanged (`asio_test.cc` passes as is; an off-loop create/destroy test); a wheel destroyed with
  deadlines armed releases them; `scheduler_ref` gains `wheel()` source-compatibly; tests in the suite.
- A4 TSan and ASan+UBSan builds of the suite pass (CI, or a job; on Linux run TSan under
  `setarch -R` if the kernel's mmap entropy trips it).
- A5 No allocation per arm, re-arm, cancel or fire: a counting-allocator test.
- A6 [artifact] `docs/timer-wheel.md` in task: the shape, the two threading contracts (other threads
  arm only an unarmed entry; only the owner cancels or re-arms), the A1 numbers.
- A7 [manual] The Xapiand lead reviews the diff (async-drivers row 1).

## Contracts

Provides `task:timer_wheel`. Kronuz/scheduler (scheduler-on-wheel) and the async-drivers quests
consume it.

## Decisions

- D70: (f) is the one wheel; re-arm gap first. D71: it lives in task.

## May land early

Tests and the bench harness port, before the API is public.
