# service-shape: journal

State: ready for review
Quest: .campaign/detent/quests/service-shape.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: the revised design is at task:ed76e4f; docs check 261112-102139-05709b passed.

## Next

1. The lead: a fresh `design` review of task:ed76e4f, and the Xapiand lead's re-read of the fit
   findings (A3), with Q3 put to them.

## Blocked on

Nothing blocks the work. Q3 (Xapiand lead): remove `/_quit` and `/_flush?clients`, or map them to
`signals.request_stop()`; recommend remove. Q1 and Q2 are gone, answered by D80.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/service-shape.md`: "API", "Shutdown, the same in all three (D80)", "Stop and drain order", "Today's shapes, mapped" (task:ed76e4f); docs check 261112-102139-05709b; signal probe 261112-101111-197ad6 |
| A2 | `docs/design/service-shape.md`: "manager.cc" (task:ed76e4f) |
| A3 | pending: design review CHANGES (F1-F5) and Xapiand fit CHANGES (B1, B2) answered at task:ed76e4f; a re-review and the Xapiand lead's agreement remain |
| A4 | `docs/design/service-shape.md`: "Timers" (task:ed76e4f), answering msg-30's N1 and AsioHost timers; G2 added |

## Result

`docs/design/service-shape.md` in Kronuz/task (task:ed76e4f), about 2,100 words outside code
blocks (300 over the 1,800 budget: D80's section and the four-call contract). Probe scripts:
`.scratch/service-shape-signals/` in the home checkout.

What each change answers (Detent review `reviews/service-shape-design.md`; Xapiand fit review
`~/code/Kronuz/Xapiand/.campaign/xapiand/reviews/detent-service-shape-fit.md`):

- F1, F12: D6 rewritten. A `sigaction` handler (`SA_RESTART`) counts and writes to a pipe; a
  signal thread requests stop, hurry, and at the third runs every `flush()`, then `SIG_DFL` and
  raise. Job 261112-101111-197ad6 (exit 0): killed by the third SIGTERM with the loop idle, wedged
  (0.61 s, before unwedging), and during teardown past `io.run()`; with a 1 s flush, killed at
  1.41 s with the flush's marker written; two signals do not kill; an early signal is seen;
  `SA_RESTART` keeps a blocked `read`. Back-to-back bursts of three killed 2 of 5 (first run 3 of
  5): POSIX merges a pending signal; in Limits.
- F2, B2, D80: `service::flush()` (any thread, blocking, idempotent) is durable work neither the
  bound nor the hurry cuts; it runs on the drained path, the unfinished path before
  `_Exit(EX_TEMPFAIL)`, and the third signal. Index commits are in the registry's drain with no
  token. D9.
- F2's time / G1: `drain(const stop_bound&)` with `cancel` (bound - grace, or hurry) and `abandon`
  (grace later); D7 argues HTTP's reserve is not needed once Stop 3 is outside the bound.
- F3: D13 and step 3 of the order: no drain wait takes the stop token; requests take `cancel`,
  cancelled work `abandon`, pacing and durable waits none. Authority, relay and agent rows remapped.
- F4: `serve(..., ready, ...)` calls `ready` after every start; `signals.fail()` lets a service end
  the process with a cause; `served.failure` carries the first start, drain or fail error. Agent,
  relay and detentd rows say where each goes.
- F5: D8, every `stop()` at the first signal before any drain; the authority's `stop()` answers
  held polls 503 while admitted writes apply; HTTP's `stop()` only closes listeners.
- F6/G6 (lead ruling): D12, `periodic(period, stop, tick, on_throw)` keeps ticking after a throw.
- F7, G5: "Signals" row: SA_RESTART, signal names logged, stdin redirect via `cancel_callback`,
  "you insisted" is the second signal, `exit(EX_SOFTWARE)` is the third; log flush in `main`.
- F8: the agent's `monitor` keeps a 20 ms status read for logs; Summary says no shutdown polls.
- F9: `teardown` removed; `main` calls the control loop's phases 2 and 3; services do their own.
- F10: D14, one time shape (`now()` ns); step 1 moves `deadline` (5 call sites outside task).
- F11: the `main` sketch assigns `result` and takes a throw from `serve`.
- F13: step 1 adds the other-scheduler root test; D10 names the assert change.
- B1: HTTP's drain ends with `HttpAsioService::stop()`, `IndexChores::cancel_all`, `close()`,
  `destroy_roots()` on a D11 thread; D10 allows phases 2 and 3 off the owner's thread.
- G2: registry timers cancelled by its drain on the live loop; `Timer` dies before the scheduler.
- G3: Q3. G4: the registry's control tasks are its `group`. G7: autocommit's own source.
- D80 / lead item 3: "Shutdown, the same in all three"; Q1 and Q2 removed.

## Rulings I made

- Detent's durable steps, checked in code: a write is acknowledged only once its journal append
  is durable (journal README:20-21, consensus README:73), and unfinished work is answered
  `outcome_unknown` (detent_authority/service.cc:2720-2800); checkpoint generations and the
  agent's table publish atomically (agent_store.cc:494-506). So Detent's `flush()` is empty: its
  drains let an in-flight append or publication finish, and a third signal that cuts one loses
  nothing acknowledged. D80 names "generation publication, journal fsync"; both complete before
  acknowledgement, not at stop.
- A 5 s grace inside the 35 s bound (cancel at 30 s, abandon at 35 s): without it a hurry would
  abandon any request still unwinding, and indexes would skip their commit; a request that never
  ends leaves the unfinished path (flush, then `_Exit(EX_TEMPFAIL)`) without needing a third
  signal, which an orchestrator sending one SIGTERM then SIGKILL never sends.
- After an unfinished drain, later services only `flush()`: their drains would wait on users the
  unfinished one left alive (Xapiand's Stop 3 waits on pins).
- `stop_signals` and `serve` move to `task_service.hh` and no longer need Asio; `serve` is
  templated on the loop, so step 3 can test it on `inline_scheduler`.
- Earlier rulings kept: two extra Detent shapes (detentd's polls, the relay's tick); D10.
