# reactor-on-wheel: Reactor loop timers on the wheel

Risk: normal. Repo `../reactor`. After task-wheel (the loop's wheel and its one driving Asio
timer come from there). The lead writes this quest; contributors read it and never edit it.
Machine state: `campaign status reactor-on-wheel`.

## Outcome

Every timer a reactor loop runs is an entry in that loop's `wheel::timer_wheel` (one timer
mechanism, D70-D72): `PeriodicTimer` and the accept-retry timer are wheel entries, and the loop's
single Asio timer, armed at `next()`, also records its own wake lateness, so `LagProbe` needs no
timer of its own. `PeriodicTimer` keeps its API and semantics (start/again/stop from any thread,
posted to the loop; ev::timer's after/repeat/again). Its one live user is Xapiand's
`src/manager.cc` (cluster, its other user, is archived).

## Acceptance criteria

- A1 `PeriodicTimer` and accept retry behave as before on the wheel: the existing events and
  callback-failure tests pass unchanged, plus a test that runs a periodic timer on a manual clock.
- A2 Re-arm cost, measured before and after (`exclusive`, D51 pairs): `again()` on the loop thread
  and from another thread, at 1 and 1,000 live timers. The Asio path pays a cancelled wait (an
  aborted handler run and a shared state copy) and the io_context's timer-queue update per re-arm;
  report the honest magnitude either way, and the share of a loop iteration it is.
- A3 `LagProbe` accuracy holds: on a loaded and an idle loop, its p50 and p99 lag with the driving
  timer as the probe are within the A/A band of the current probe's (an `exclusive` A/B), since the
  wheel quantizes to 1 ms and the probe must not.
- A4 [artifact] `ARCHITECTURE.md` says the loop owns one wheel and one Asio timer, and why the lag
  probe rides that timer: `ARCHITECTURE.md`.
- A5 [manual] The Xapiand lead confirms `src/manager.cc` builds and passes its tests at the new
  reactor pin (send the diff via `campaign message xapiand: ...`).

## Contracts

Consumes `wheel:timer_wheel`. `reactor::PeriodicTimer`'s API is unchanged.

## Decisions

D70-D72 (one wheel), D75. Owner, 2026-11-12: plan PeriodicTimer on the wheel if it has
advantages (one mechanism, no per-re-arm cancelled-wait churn, a manual clock for deterministic
tests); A2 states their size.

## May land early

Tests (A1's manual-clock test against today's PeriodicTimer is not possible; the A2 benchmark
harness is).
