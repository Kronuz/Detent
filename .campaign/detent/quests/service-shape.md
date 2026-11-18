# service-shape: One service and housekeeping shape for Prism, Detent and Xapiand

Risk: high (a shared public API across three projects). Repo `../task`. After task-wheel. Design
assignment; the lead writes this quest; contributors read it and never edit it. Machine state:
`campaign status service-shape`.

## Outcome

`docs/design/service-shape.md` in Kronuz/task fixes one shape (D76) that Prism, Detent's agent and
authority, and Xapiand all migrate to, each in its own later quest:

1. `main` installs signals once and turns them into one stop request; a second signal hurries.
2. A service contract: `start()`, `stop()` (request), `drain(deadline)` (bounded, ordered).
3. `Periodic` and `Debounce` as tasks awaiting the loop's timer (the wheel, D71), moved from
   Xapiand's `src/control_plane.h` into Kronuz/task.
4. Shutdown is cancellation plus the bounded drain; no polling timers (Detent agent's 20 ms drain
   poll, Prism's 100 ms signal poll) and no `reactor::PeriodicTimer`.

## Acceptance criteria

- A1 [artifact] The design names the API (signatures), the stop and drain order with its single
  bound, how a second signal hurries, and where each of today's four shapes maps:
  `docs/design/service-shape.md`.
- A2 [artifact] A mapping table for Xapiand's `manager.cc` duties (msg-33 reply): ordered stop
  (D16, `join()` :1033), autocommit debounce (:301), node identity, server wiring (:895), index
  settings resolution (:1257-1275, to the index resolver), metrics; each to its new owner:
  `docs/design/service-shape.md`: "manager.cc".
- A3 [manual] One `design` review accepted, and the Xapiand lead agrees the shape before any
  migration quest starts.

- A4 [artifact] The design answers task-wheel A7's notes (msg-30): Xapiand's Periodic, Debounce,
  SleepFor and registry timers run on `AsioHost::arm` (an Asio timer per use), not the wheel, so
  the shape says which one timer they move to; and a closed `asio_scheduler`'s wheel never fires
  while Xapiand reruns its control loop after closing (manager.cc:1119/1131), which the drain order
  must not depend on: `docs/design/service-shape.md`: "Timers".

## Contracts

Provides `task:service-shape` (Periodic, Debounce, the service contract). Consumes
`wheel:timer_wheel` through task-wheel.

## Decisions

D71, D76. Xapiand lead (msg-33 reply, 2026-11-12): design first as a shared piece, then each
repo migrates; Xapiand's manager.cc quest waits on this and its Phase 7c gate.

## May land early

Nothing: the design lands whole after its review.
