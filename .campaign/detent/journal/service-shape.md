# service-shape: journal

State: ready for review
Quest: .campaign/detent/quests/service-shape.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: the design is at task:5f07955 (`docs/design/service-shape.md`), docs check job
261112-093553-bd40d1 submitted.

## Next

1. The lead: one `design` review of task:5f07955, then the Xapiand lead's agreement (A3), with
   Q2 put to them.

## Blocked on

Nothing blocks the work. Two questions in the design need answers before migrations start:
Q1 (owner: the stop bound for Prism and Detent, today unbounded; recommend 30 s with a flag) and
Q2 (Xapiand lead: one bound from the first signal covers the in-flight wait that
`shutdown_checks` does today, and the acceptors close at the first signal; recommend accept).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/service-shape.md`: "API", "Stop and drain order", "Today's shapes, mapped" (task:5f07955); docs check 261112-093553-bd40d1 |
| A2 | `docs/design/service-shape.md`: "manager.cc" (task:5f07955) |
| A3 | pending: design review and the Xapiand lead's agreement (the lead arranges both) |
| A4 | `docs/design/service-shape.md`: "Timers" (task:5f07955), answering msg-30's N1 and AsioHost timers |

## Result

`docs/design/service-shape.md` in Kronuz/task (task:5f07955), about 1,800 words outside code
blocks. It defines `sleep_until`/`sleep_for`, `event::wait`, `group`, `periodic`, `debounce`,
`drained`, the `service` concept, `teardown`, `stop_signals` and `serve`, with signatures; the stop
order with one bound (a `deadline` whose parent is the hurry token); and mappings for the Detent
authority, detentd, the agent, the relay, Prism, manager.cc's duties and Xapiand's timers.
D1-D12, Q1-Q2, first buildable steps. No measurement was made; the only number cited (wheel
lateness 0.5-2.2 ms) is from task's `docs/mechanisms/deadlines.md`.

## Rulings I made

- Two more Detent shapes found by reading, beyond the quest's four: detentd/main.cc polls a flag
  every 5 ms and 200 ms (:198-209); the relay has its own signal_set and 1 ms tick
  (detent_relay/host.cc:285-375). Both are mapped.
- New header `task_service.hh` for group/periodic/debounce/service/teardown, so task.hh gains only
  sleep and `event::wait`; `stop_signals` and `serve` go in task_asio.hh (they need Asio).
- Signals through `asio::signal_set`; after the second signal it is cleared, which restores
  SIG_DFL (verified in Asio 1.36.0 signal_set_service.ipp:456-465,515-522), so a third ends the
  process even with the loop wedged.
- `asio_scheduler::cancel` allowed on the teardown thread once closed (D10), so a sleep's entry
  can live in the frame; needed because phase 3 destroys sleeping roots.
- `periodic` ends on a throw (D12), unlike control::Periodic; Xapiand's housekeeping tick catches
  what it tolerates.
- Teardown phases 2 and 3 cross services, so the design adds `teardown(...)` beyond the quest's
  start/stop/drain; `asio_scheduler` fits it as it is.
- Limits names a cost to measure: a 1 ms wheel sleep takes 1-2 ms, so Detent's 1 ms authority and
  relay loops measure throughput before switching (D1's measured-reason escape).
