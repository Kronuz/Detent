> Status: CURRENT (2026-11-03) how `detent::OwnerLoop` and `detent::owner_submit` hand serialized work to an owner and wait for it, read from `include/detent/owner_loop.hh` at Detent:2fc6269.

# OwnerLoop and owner work as a child task

An owner is the one thread that serializes some state: the relay's coordinators and the authority
runtime. Owners do blocking file I/O, so they are not `reactor::Reactor`s
and `reactor::inside_run()` stays false on them. A request handler runs on an HTTP reactor's
loop and must never block it. It hands the owner a piece of work as a child task and suspends on
an event until the owner finishes, a shutdown is requested, or a backstop deadline passes.

## OwnerLoop

`OwnerLoop` (`owner_loop.hh:24`) is an `asio::io_context`, a `task::asio_scheduler` on it, a work
guard and one thread running `io_.run()`. Members are declared in that order, so the thread
starts last and joins first. `io()`, `scheduler()` and `inside()` (the scheduler's `runs_here()`)
are the accessors.

It exposes task's teardown phases as three calls (`asio-scheduler-teardown` in task's
`docs/mechanisms/`):

- `stop()`: release the work guard, stop the loop, join the thread; queued handlers are dropped.
- `close()`: the scheduler drops what is posted and destroys what is spawned.
- `destroy_roots()`: destroy every task still suspended on the loop, `io_context` alive.

The destructor runs all three, so a lone `OwnerLoop` needs no calls. The owner of several loops
runs each phase on all before the next (`teardown.md`).

## owner_submit

```cpp
Reply r = co_await detent::owner_submit<Reply>(
    owner,
    [&] { return accept(request); },                       // on the owner: returns shared_ptr<State>
    [&](State& s, task::cancel_reason why) { return reply_for(s, why); },
    backstop_at, shutdown_token);
```

`owner_submit` (`:69`) is a `task<Reply>` the handler awaits. In order:

1. `co_await schedule_on{owner.scheduler()}`: the child moves to the owner's loop.
2. `accept()` runs there and returns the per-request state, a `shared_ptr` whose `State` has a
   `task::event done`. A completion (the owner's, or another thread's) settles the state and calls
   `done.set()`.
3. A `task::deadline backstop` on the owner's scheduler, linked to the service's shutdown token
   `stop`, so the backstop token is requested by `stop` or by the deadline, whichever comes first
   (`task::deadline`, task's `cancellation.md` in `docs/mechanisms/`).
4. A `task::cancel_callback` on the backstop token calls `done.set()`.
5. `co_await state->done`: one wake from three sources, the owner's completion, the shutdown and
   the backstop. `event::set` is idempotent and callable from any thread.
6. `co_return finish(*state, backstop.token().reason())`, still on the owner. `reason` is `none`
   when only `done` fired; otherwise `shutdown` (from `stop`) or `deadline`. `finish` sees the
   state the owner left and the reason, and builds the reply: the completed one, or a truthful
   one for a wait that ended early.

When the child ends on the owner's scheduler, task posts the handler back to its own loop, because
the awaiter's scheduler differs. Only the owner thread touches the state, apart from `done`.

## Where it is used

Every owner is an `OwnerLoop`: the relay host's owner (`src/detent_relay/host.cc`; the coordinators run
on its scheduler), the authority `Service` (`service.cc`, `handle`: commands, reads and results hop to the runtime's
loop, which is the `Service`'s own `OwnerLoop`), each `fleet_load` owner and the teardown tests.

- Authority: `prepare` parses on the handler's loop; `accept` queues the `Task` for the runtime;
  the runtime's `complete` sets `Task::outcome` and `done`. `finish` (`reply_for`) reserves the reply
  against `reply_bytes` and returns it with the reservation; a wait that ended on the stop is answered
  buffered, any other reply leaves through an owned stream (`write_owned`).

## Properties

- A stop requested before `accept()` still runs `accept()` and then ends the wait at once (the
  deadline is born requested, the callback fires in its constructor), so `finish` sees accepted
  work and can say its outcome is unknown rather than refuse.
- The state is owned by a `shared_ptr` the owner keeps, so a completion after the wait ended sets
  an event nobody waits on.
- The callback and the deadline are locals of the child frame; destroying the frame (teardown)
  deregisters both, and `~deadline` may run on any thread.
- Registering the callback allocates nothing.

## Invariants

1. The wait ends on exactly one of completion, stop, backstop; the reply is built once, on the
   owner.
2. No Detent completion type: the wait is `schedule_on`, a `task::event` in existing per-request
   state, a `task::deadline` and a `cancel_callback`.
3. The state outlives the wait: the owner holds it through the `shared_ptr`.
4. Teardown destroys a wait that is still suspended without touching freed state (task's phase 3).

## Tests

`tests/detent_owner/test_owner_wait.cc` (ctest `test_owner_wait`), with a plain loop standing in
for the HTTP reactor: (b) a stop wakes the wait within 50 ms, while the child waits, before it
reaches the owner, and before the handler starts (1); (a) a completion settled before and after the
child starts waiting; (c) a completion after the wait ended is clean (3); (d) a backstop expiry
answers the backstop reply, and a completion followed by the backstop changes nothing (1); a
race of completions against a stop on three threads ends each request with one reply (1). Each
case checks `live_frames()` and `orphaned_waiters()` back at their baseline (4).
