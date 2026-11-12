# service-shape review: design

Commit: task:5f07955 (5f079559844f91ad80e5a494f356bccc9d3725a5, `docs/design/service-shape.md`)
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The shape is right: one control loop, a token for stop and one for hurry, start/stop/drain in
order under one deadline, every wait a wheel entry in its frame. D10 holds (probe below). What
blocks is where the design meets today's code: the third signal does not work in the case it is
there for, the one bound cuts Xapiand's WAL join, waits that run during a drain are given the stop
token, and nothing in the contract carries readiness, a service's own end, or a failure exit.
Each is a text fix in the design; none changes the shape.

## Evidence I relied on

None from the contributor: the quest's only job at this commit (261112-093553-bd40d1, the docs
check) proves formatting, not behavior. I read the design against task:47d50f3 (`task.hh`,
`task_asio.hh`), wheel:bfd72bc, Asio 1.36.0, Xapiand:000224f5a, Detent:35c933ca and prism:e0aea0e.

## What I checked myself

Scripts under `.scratch/review-service-shape-design/` in the home checkout:

- `run_signals.py` + `signals_probe.cc`, job 261112-094453-310c13: the design's `stop_signals`
  (D6) modeled on Asio 1.36: an `asio::signal_set` on the loop, the first SIGTERM is stop, the
  second `clear()`s the set. Three SIGTERMs 200 ms apart with the loop idle, with the loop thread
  blocked 3 s after the first (a drain that blocks), after `io.run()` returned (teardown), and as
  a back-to-back burst; plus one signal before `io.run()`.
  Result (exit 0, log `~/.scratch/campaign/jobs/261112-094453-310c13/a1/log`): idle loop, 200 ms
  apart: killed by the third (rc -15, 0.61 s). Wedged loop: not killed (rc 0; the loop saw only two
  signals after it unwedged). After `io.run()` returned: not killed, 0 signals seen (rc 0). Burst
  of three: killed 0 of 5 runs (the loop saw two; the third was swallowed). Early signal: delivered
  once the loop ran.
- `run_d10.sh` + `d10_probe.cc`, job 261112-094456-971fc0: at task main, a root sleeps on an
  `asio_scheduler` wheel entry held in its frame; phases 1-4 run with the awaiter's destructor
  cancelling the entry on the teardown thread after `close()` (D10), under ASan; a control without
  that cancel; and the raw wheel cancelled on another thread after `release()` in Debug.
  Result (exit 0): with the destructor's cancel, ASan-clean (`armed after close: 1`, `after
  destroy_roots: 0`); without it, ASan use-after-poison in `~timer_wheel` (`wheel.hh:81`) from
  `~asio_scheduler`, so D10 is necessary; the raw wheel cancels on another thread after `release()`
  in Debug; and at main `asio_scheduler::cancel` asserts (`task_asio.hh:148`) after close, the
  change step 1 must make.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The third signal does not end a wedged process. Asio's OS handler only writes to its pipe; `SIG_DFL` comes back when the loop runs signal 2's completion and `clear()`s. With the loop thread blocked (the Limits case, "only the third signal ends it", design :202-203) signals 2 and 3 are swallowed; after `serve` returns and `io.run()` exits, every signal is swallowed until `~stop_signals`, so a wedged teardown cannot be interrupted; a burst of three inside the loop's latency is swallowed too. Today Xapiand exits from its handler once the manager is deinited (`main.cc:251-262`). | Job 261112-094453-310c13; `signal_set_service.ipp` handler (write to pipe) vs `clear()` at :515-522; design :127-129, :188 | Escalate in the handler, not on the loop: `stop_signals` installs its own `sigaction` handler that counts in an atomic, writes a byte to a pipe the loop awaits (`await_asio` on a `stream_descriptor`), and on the second signal resets the disposition to `SIG_DFL` itself (`sigaction` is async-signal-safe). D6's "a self-pipe re-implements signal_set" no longer holds: signal_set cannot do this. Test: three SIGTERMs with the loop blocked in a drain kill the process; three during teardown after `serve` returns kill it. |
| F2 | yes | defect | The one bound under-drains Xapiand's writes. Stops 4 and 5 (WAL writers, fsync threads) are unbounded today (`manager.cc:1160`, `:1179`) and still run after an abandoned stop, which is why "what was acknowledged is in the log writers' files" before `_Exit(EX_TEMPFAIL)` (`:1188-1195`). The design puts Stops 3-5 in the registry's drain under the one bound, where a requested bound "waits for nothing" (design :138-139, :168): a hurry, or an HTTP drain that spends the bound, skips the WAL join and exits with acknowledged log writes still queued. And `drain(cancel_token)` carries no time, so HTTP cannot size its grace from what is left (`http_shutdown_grace(left)`, `:148`, `:165-169`; D16's W3 arithmetic and 5 s reserve), so HTTP can spend the whole bound. | `manager.cc:1033-1195`; Xapiand `docs/design/phase7c.md` D16 (:387-410) | Say which drain steps the bound may cut (requests, index close) and which it may not (the WAL writers' and fsync joins, ended only by the third signal); give `drain` the bound's time as well as its token (for example `drain(const deadline&)` or `(token, time_point)`) so HTTP keeps the reserve. Test: after a hurry, the registry's drain still joins a WAL writer that holds an acknowledged write. |
| F3 | yes | defect | Waits that run during a drain are mapped to the stop token. The authority's retirement loop runs after `stopping_` until the node drains (`service.cc:2772-2785`, the design's cited :2784); the relay keeps turning until every coordinator drains after `shutdown()` (`host.cc:276-283`, `:355-375`); the agent's `monitor` runs until `coordinator->drained()` and then decides the exit status (`managed_host.cc:39`, `:110-124`). The design maps them to `sleep_until(at, stop_.token())`, `sleep_for(1ms, stop)` and `periodic(20ms, stop, report)` (design :26-29). Once stop is requested each sleep returns false at once (design :38-39), so those loops spin without the 1 ms pacing or end before the drain is done, and the agent's terminal checks (preparation timed out, fenced, failed before ready) never run. | Cited lines; design :26-29, :38-39 | State the rule: a wait inside a drain takes the bound's token, not the stop token. Remap the three rows: the authority's and relay's drain loops sleep on the bound; the agent's terminal checks move into its `drain()`. Test (step 1 or the migrations): a drain loop sleeping 1 ms after stop still sleeps about 1 ms per turn. |
| F4 | yes | gap | The contract has no readiness point, no way for a service to end the process, and no failure exit. detentd calls `ready()` after start and the lone-quorum wait and turns `authority.failure()`, or a stop before startup when detached, into a failure exit (`detentd/main.cc:198-206`, `:223`); the agent announces ready from `monitor` (`managed_host.cc:43-56`), and its run ends without a signal when the coordinator drains (fence, preparation timeout), throwing the cause as the exit status (`:110-124`, `:226-227`); the relay's failure calls `shutdown()` (`host.cc:371-372`). `serve` waits only for `signals.stop()`, returns only `drained.unfinished`, and gives `main` no point between "all started" and "waiting" to call `ready()`. Risk 4 named the agent's ready announcement: it is unmapped. | Cited lines; design :79-107, :109-122 | Name the three: a `started` point (for example `serve(..., on_started)`, or `main` awaits an `event` that `serve` sets), a service ending the process through `signals.request_stop()`, and `serve`'s result carrying the first service failure (`exception_ptr`) for `main`'s exit status. Add the agent and relay rows that say where each goes. |
| F5 | yes | gap | detentd's stop order is reversed and the consequence is not argued. Today `authority.begin_shutdown()` comes before `service.stop()` (`detentd/main.cc:215-216`); the authority's stop is what closes subscriptions and answers held session and poll requests 503 (`service.cc:2691-2701`). Under D8, `serve(signals, bound, authority, publisher)` drains the publisher first while the authority still serves, and a handler waits on `owner_submit` with only `stop_.token()` and a deadline of `options_.timeout` (up to 24 h, `service.cc:1134-1150`, `:569`). Unless HTTP's stop ends those waits some other way, each shutdown with an open poll runs to the bound and exits `EX_TEMPFAIL` without teardown (D9). | Cited lines; design :27, :135-139, :190 | Settle it in the design: either the authority's `stop()` keeps answering held polls while still applying admitted writes, called before the publisher's drain (a "stop all in reverse, then drain in reverse" order, with Xapiand's registry `stop()` still admitting in-flight writes), or HTTP's drain cancels each request through a token the authority's waits observe. Test: detentd with an open subscription poll stops in well under the bound. |
| F6 | no | nit | D12 narrows Xapiand's housekeeping: today a throwing tick is logged and the next still runs (`control_plane.h:101-102`); under D12 it ends the `periodic`. If `group::failed` is wired to `request_stop` (the "control task that fails" row, design :177), a `schemas->cleanup()` throw would stop the node, where today maintenance failures are only logged (`manager.cc:728-729`). The owner's call: keep continue-after-throw for `periodic`, or accept D12 and say that `failed` only logs for maintenance groups. | `control_plane.h:101-102`; `manager.cc:728-735` | Options above, including keeping today's behavior. |
| F7 | no | nit | Xapiand's handler side effects are not in the "Signals" row: stdin goes to `/dev/null` on SIGINT/SIGTERM (`main.cc:237-243`), and the "you insisted" escalation (`manager.cc:646-662`). | Cited lines | Map them or say they are dropped. |
| F8 | no | nit | The agent's `monitor` stays a 20 ms status poll (`periodic(20ms, ...)`), while the Summary says nothing polls (design :19). | design :19, :28 | Say it is accepted (status diffing for logs), or name the event it waits on. |
| F9 | no | nit | `teardown(S&...)` calls `close()` and `destroy_roots()` on each argument, a requirement outside the `service` concept, and is two loops. | design :86-87, :121 | Constrain it to schedulers (things with `close`/`destroy_roots`), or drop it and write the two calls in `main`. |
| F10 | no | nit | Two time shapes: `sleep_until` takes `now()` nanoseconds; `deadline` takes `clock::time_point`. | design :40; `task.hh:1285` | One of them. |
| F11 | no | nit | The `main` sketch never assigns `result`, and shows no exit path when `serve` throws (a failed start). | design :116-120 | Fill both in. |
| F12 | no | nit | D6's "Asio restores `SIG_DFL`" holds only when the set is the process's last registration of that signal (`signal_set_service.ipp:456-465`). Moot if F1 is fixed as proposed. | Cited lines | Say so in D6 or Limits. |
| F13 | no | nit | D10 is sound (job 261112-094456-971fc0): the wheel's `release()` lets the teardown thread become its owner, and the frame's cancel leaves nothing for phase 4 to fire. Step 1 must relax `asio_scheduler::cancel`'s assert (`task_asio.hh:148`) to allow a closed scheduler, and its test should include a sleep whose frame is a root of another scheduler. | Job 261112-094456-971fc0 | As stated, in step 1. |

## Not run

- No build of any real user; no Detent HTTP library read to settle whether `HttpAsioService::stop`
  ends an in-flight Detent handler's `owner_submit` wait (F5 is a gap for that reason).
- Signals were probed on macOS only (kqueue reactor); Linux's epoll reactor delivers the same way
  through the pipe, but no Linux run.
- Wheel lateness and loop-throughput claims (Limits) were not measured; they are the migration
  quests' to measure.
- Xapiand's metrics and node-identity rows were read for presence, not traced call by call.
