# reactor-on-wheel: journal

State: waiting on jobs 261117-083939-245653 261117-083948-d8d15e (suite and TSan at reactor:bf874dc); A2 needs the lead (Blocked on)
Quest: .campaign/detent/quests/reactor-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged origin/main (CHANGELOG and CMakeLists conflicts; task pin 18eb516 contains the wheel's 0e71a38) and
removed PeriodicTimer with its tests and doc mentions (reactor:0d3bc71); fixed lag_ab.sh (reactor:bf874dc).
In-repo `git grep PeriodicTimer` finds only CHANGELOG.md. Suite 261117-083939-245653 and TSan 261117-083948-d8d15e
queued at bf874dc.

## Next

1. When both jobs pass: `campaign brief`, paste its Evidence table, set State "ready for review".
2. A2: see Blocked on.

## Blocked on

A2's exclusive A/B 261112-115020-3a3dea failed (exit 1): `benchmarks/lag_ab.sh` read `$ASIO` in run mode under
`set -u`. Fixed in reactor:bf874dc (flags set only in build). Resubmitting it
(`campaign job submit --quest reactor-on-wheel --for A2 --level exclusive --minutes 10 -- sh benchmarks/lag_ab.sh run
/Users/kronuz/.scratch/reactor-on-wheel-ab 10`; binaries a, b still in that directory from build job
261112-115009-0a0428 at e487a46, lag code unchanged since) was denied by the permission classifier; the lead must
submit it or approve it. Not in this assignment's scope.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | suite job 261112-115026-65553f (ctest in Release: events, callback_failure, accept_pressure incl. the manual-clock retry) |
| A2 | pending: exclusive job 261112-115020-3a3dea |
| A3 | ARCHITECTURE.md, section "Timers: one wheel, one Asio timer per loop" (reactor:99aff1b) |
| A4 | manual: lead grep 2026-11-17 over Kronuz default branches: no code user (Xapiand:f5e267411 moved manager.cc; cluster archived); in-repo `git grep PeriodicTimer` at reactor:bf874dc names only CHANGELOG.md (reactor:0d3bc71 removed it, its tests and docs) |

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
