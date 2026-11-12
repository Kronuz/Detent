# reactor-on-wheel: journal

State: waiting on jobs 261112-115020-3a3dea 261112-115026-65553f
Quest: .campaign/detent/quests/reactor-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and docs done on the quest branch (reactor:99aff1b): accept retry and LagProbe are wheel
entries; ARCHITECTURE.md, API, AGENTS, stop-and-accept note, CHANGELOG updated. Suite passed
(261112-115026-65553f, Release). A local Debug+ASan+UBSan run of lag, accept_pressure, session and
reactor tests passed (not evidence; sanitizers are CI's). Waiting on the exclusive A/B
261112-115020-3a3dea (queued behind light holds; `benchmarks/lag_ab.sh run`, a a b b twice, idle and loaded).

## Next

1. When 261112-115020-3a3dea ends: read its lines (p50, p99, mean per mode, A vs A/A band), write A2's numbers in
   Result and Evidence; if B is outside the A/A band, investigate before anything else.
2. Run `campaign brief`, paste its Evidence table, set State "ready for review".
3. A4 stays open until Xapiand's manager.cc moves (see Blocked on).

## Blocked on

A4 (remove PeriodicTimer): its last user is Xapiand's src/manager.cc (try_shutdown_timer, `reactor_events.h` include), moved by the Xapiand campaign. Not started here; A1-A3 may land early.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. LagProbe is a wheel entry, not a change to the loop's Asio timer: that timer is private to task's
   `asio_scheduler` (D79), so the probe aims at `target = floor((now+interval)/1 ms)*1 ms`, arms at
   `target - 1 ns` (its slot ends at `target`, where the scheduler sets the timer) and records `now - target`.
   That is the timer's own lateness without the wheel's slot rounding. No task change.
2. `LagProbe(task::scheduler_ref)` replaces `LagProbe(io_context&)`; `Reactor::lag()` and
   `TcpServer::enable_lag_probe()` are unchanged. `start()` posts the arm to the loop off-thread. The probe
   never touches the scheduler in its destructor (the Reactor declares it before the scheduler, so a dying wheel
   fires it with expired false). Recorded in CHANGELOG.
3. The probe's "allocates nothing" holds for the probe; the loop's one Asio timer (task) allocates an asio
   operation per wake, which asio's cache recycles. lag_test [C] counts allocations after warm-up and no longer
   defines ASIO_DISABLE_SMALL_BLOCK_RECYCLING.
4. task:0e71a38 has no sleep, periodic or debounce (the assignment said it did): the accept retry uses its own
   `detail::WheelSleep` awaiter over `scheduler_ref::arm/cancel`. Entry lifetime: the wheel holds a shared_ptr
   while armed, so a frame destroyed by destroy_roots does not leave a dangling entry.
5. The accept retry's per-connection 1 ms pause now ends 1 to about 2 ms after it is due (wheel tick); noted in
   stop-and-accept.md.
6. benchmarks/lag_bench.cc and lag_ab.sh (new, in the file set) carry the A2 measurement.
