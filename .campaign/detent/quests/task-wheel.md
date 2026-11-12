# task-wheel: task on Kronuz/wheel

Risk: high (concurrency, a shared public API). The lead writes this quest; contributors read it and
never edit it. Machine state: `campaign status task-wheel`.

## Outcome

Kronuz/task depends on Kronuz/wheel (D72; wheel-lib) and `task::deadline`, `inline_scheduler` and
`asio_scheduler` run on it (async-drivers quest 1, `docs/design/async-drivers.md` table row 1). The
wheel's own code, A1 (re-arm), A2 (model), A5 (no allocation) moved to wheel-lib; this quest keeps
the integration written at task:5a1e516 and drops its inline copy of the wheel.

## Acceptance criteria

- A1 task fetches Kronuz/wheel at a pinned commit and contains no wheel code of its own (grep job).
- A3 Integration per async-drivers row 1: `deadline<S>` on `S::clock`; `inline_scheduler::advance`
  expires with no real time; `asio_scheduler::start_timer` on the wheel with the deadline contract
  unchanged (`asio_test.cc` passes as is; an off-loop create/destroy test); a wheel destroyed with
  deadlines armed releases them; `scheduler_ref` gains `wheel()` source-compatibly; tests in the suite.
- A4 TSan and ASan+UBSan builds of the suite pass (CI, or a job; on Linux run TSan under
  `setarch -R` if the kernel's mmap entropy trips it).
- A6 [artifact] task's docs name Kronuz/wheel and the deadline contract on it.
- A7 [manual] The Xapiand lead reviews the diff (async-drivers row 1).

## Contracts

Consumes `wheel`. Kronuz/scheduler (scheduler-on-wheel) and the async-drivers quests
consume it.

## Decisions

- D70: (f) is the one wheel. D72 (supersedes D71): the wheel is Kronuz/wheel; task depends on it.

## May land early

Tests and the bench harness port, before the API is public.
