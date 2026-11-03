> Status: CURRENT (2026-11-03) how the coordinator, the managed host and the relay host shut down and run task's teardown phases, and how the live-frame check proves it, read from `src/` and `tests/` at Detent:2fc6269.

# Coordinator, managed host and relay owner teardown

Frames in Detent cross schedulers: a handler's child waits on an owner's event, a coordinator's
tasks wait on timers and sockets of an owner's `io_context`. One scheduler's destruction order is
not enough, so each host runs task's four phases (task's `docs/mechanisms/asio-scheduler-teardown.md`):
stop and join every loop; `close()` every scheduler; `destroy_roots()` on every one with every
`io_context` alive; then destroy the objects the tasks referenced. The hosts reach the phases only
after an orderly drain, and `destroy_roots()` is what ends whatever is still suspended.

## The coordinator's own shutdown

`Coordinator::run()` (`coordinator.cc:1154`) returns a task that `when_all`s `disk_loop()` and
`candidate_loop()` on the owner's scheduler (`run_owned`, `:1164`), then sets `settled` and the
phase (`Closed` or `Fenced`). `drained()` is `settled && store->drained()` (`:1217`).

`close()` (`:690`) stops construction, sets `closing`, fences the publication sink, bumps the
cancellation counter, requests `network_cancel` (the per-round source, `round-cancel.md`), wakes
the candidate and disk waits and closes the store. The loops end at their next check. `close()`
and `cancel_update()` run on the owner's executor only (`coordinator.hh:70`); the hosts call them
from the owner.

## Managed host

`run_managed_owned` (`managed_host.cc:116`) runs one `io_context` and a `task::asio_scheduler` on
the calling thread. `supervise` is a task that `when_all`s `coordinator->run()` with `monitor`
(`:110`); it is spawned on the scheduler (`:175`). SIGINT or SIGTERM calls `coordinator->close()`.
A 20 ms timer (`watch`) cancels the signal wait once the coordinator is drained, so `io.run()`
returns. Phase 1 is that return. Then:

```cpp
io.run();               // phase 1: the loop has stopped
scheduler.close();      // phase 2
scheduler.destroy_roots();  // phase 3, io_context alive
// then: join the worker, reset coordinator, driver, queue, store; close the domain
```

(`:195-206`). If the supervisor never settled and nothing failed, the run throws
"managed delivery stopped before it drained".

## Relay host

`run_host_owned` (`host.cc:126`) declares an `OwnerLoop owner` first, so it is destroyed last
(`owner-loop.md`). Each record's coordinator is spawned on `owner.scheduler()` from the owner
thread (`:368`); a failure in a coordinator calls `shutdown()`.

1. `shutdown()` (a signal, or a failure) closes the runtime and every coordinator and releases the
   main thread (`:259`). The main thread calls `listener.stop()`, which joins the HTTP service's
   loops, and posts `listener_stopped` to the owner.
2. The owner's `turn` ends the loop only when `listener_stopped`, the runtime and every
   coordinator are drained (`:336`): it cancels the signals, calls `owner.io().stop()` and sets
   `owner_done`.
3. Phases 1-3 on the owner, io alive, before anything the coordinators reference is destroyed
   (`:383-388`): `owner.stop()` (join), `owner.close()`, `owner.destroy_roots()`.
4. Phase 4: the facade, runtime, session bindings, registry, coordinators, bundles and stores are
   released in that order; the completion scheduler must be idle; the driver and queue go; the
   retirement domains close. The listener's own `close()` and `destroy_roots()` run in its
   destructor at the end of the function.

`fleet_load`'s `Run::teardown` (`benchmarks/fleet_load.cc:161`) is the same shape over several
owners: stop all, close all, destroy all, then the control loop.

## The live-frame check (T13)

Non-release builds define `TASK_COUNT_FRAMES=1` for every target (`CMakeLists.txt:20`), so
`task::detail::live_frames()` counts the pooled blocks alive (frames, lock waiters, cancel
states) and `orphaned_waiters()` the events destroyed with waiters.

- `test_coordinator_teardown` (`tests/detent_delivery/test_coordinator_teardown.cc`): an
  `OwnerLoop` runs a coordinator whose endpoint refuses connections, once it is in its reconnect
  wait. The test stops without `close()`, runs `stop()`, `close()`, `destroy_roots()` on the owner,
  joins the worker, releases the coordinator and driver with the loop alive, and requires
  `live_frames()` at its baseline and `orphaned_waiters()` 0. `DETENT_TEARDOWN_SKIP` (`stop`,
  `close`, `destroy`) removes one phase.
- `run_managed` and `run_host` (`managed_host.cc:212`, `host.cc:414-419`) take the baseline before the
  owned run and `assert` it after (Debug). The check follows the run because an expiry Asio already
  queued holds a counted block until its `io_context` is destroyed. It is end-of-function and
  compiled out under `NDEBUG`.

## Invariants

1. No frame is destroyed while another thread runs a handler against it: phase 1 joins every
   loop first, and `destroy_roots()` runs with every `io_context` alive.
2. `close()` precedes `destroy_roots()` on every scheduler: a frame destroyed in phase 3 may spawn
   onto another loop, and a closed scheduler destroys that task unrun.
3. The coordinators' dependencies (store, driver, queue, domain, facade) are destroyed after
   phase 3.
4. After the phases and the release of the coordinator, no task frame and no orphaned waiter
   remains.
5. `close()` and `cancel_update()` run on the owner.

## Tests

- `test_coordinator_teardown` (1, 4).
- `test_owner_wait`, the case "phase 2: close() across two loops" (2): a frame on loop A, destroyed in
  `destroy_roots()`, spawns onto loop B after B's roots were destroyed; with `close()` on both
  loops no frame is left (`DETENT_OWNER_SKIP_CLOSE=1` omits the calls and the case fails).
- `test_managed_*_process` and `test_relay_*_process`: the process fixtures run `run_managed` and
  `run_host` in Debug, so the end-of-function frame check (4) runs after each.
- `test_delivery_coordinator` (`test_coordinator.cc`, `observe`, `:316-326`): after `close()` the
  coordinator drains within 10 s while a network call is in flight (5).
