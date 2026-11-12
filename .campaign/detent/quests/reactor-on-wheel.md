# reactor-on-wheel: Reactor loop timers on the wheel

Risk: normal. Repo `../reactor`. After task-wheel (the loop's wheel and its one driving Asio
timer come from there). The lead writes this quest; contributors read it and never edit it.
Machine state: `campaign status reactor-on-wheel`.

## Outcome

Every timer a reactor loop runs is an entry in that loop's `wheel::timer_wheel` (one timer
mechanism, D70-D72): the accept-retry timer is a wheel entry, and the loop's single Asio timer,
armed at `next()`, also records its own wake lateness, so `LagProbe` needs no timer of its own.
`reactor::PeriodicTimer` is removed (D76): periodic work is a task awaiting the loop's timer.
Removal waits until no user remains: Xapiand's `src/manager.cc` shutdown retry is its last.

## Acceptance criteria

- A1 Accept retry behaves as before on the wheel: the existing events and callback-failure tests
  pass, plus a test that drives an accept retry on a manual clock.
- A2 `LagProbe` accuracy holds: on a loaded and an idle loop, its p50 and p99 lag with the
  driving timer as the probe are within the A/A band of the current probe's (an `exclusive`
  A/B), since the wheel quantizes to 1 ms and the probe must not.
- A3 [artifact] `ARCHITECTURE.md` says the loop owns one wheel and one Asio timer, why the lag
  probe rides that timer, and that periodic work is a task, not a timer object: `ARCHITECTURE.md`.
- A4 [manual] `PeriodicTimer` is gone from reactor, and a grep over the Kronuz repositories
  finds no user of it (Xapiand's `manager.cc` moved first, by the Xapiand campaign); archived
  repositories (cluster) are frozen and do not count.

## Contracts

Consumes `wheel:timer_wheel`. Removes `reactor::PeriodicTimer` (a public API; owner's D76).

## Decisions

D70-D72 (one wheel), D75, D76 (one housekeeping shape; PeriodicTimer retires).

## May land early

A1 and A2 (accept retry and the lag probe on the wheel) before A4's removal.
