# loop-idle-waits: journal

State: waiting on jobs 261119-101104-bee765 (suite), 261119-101125-b77320 261119-101127-d38033 261119-101129-f841a2 261119-101131-0a5215 261119-101134-5d5c49 261119-101136-147391 (A2 mutants), 261119-101138-d37b48 (A1), 261119-101145-1602c6 (A3); CI at Detent:30391330
Quest: .campaign/detent/quests/loop-idle-waits.md (the lead's; never edit it)

## Doing

Fix round 1 at Detent:30391330 (merge of main at e3e2808e). Jobs queued.

1. Suite hang (261119-072616-b3ced4: worker_lifetime, repair x5, relay_delta_shutdown_published):
   a worker job's `task::event` wait registered no work on the owner's io_context, so a loop with
   nothing else queued (run_phase's and test_worker's `io.run()`, at close with a scrub or recovery
   worker in flight) ran out of work and returned under the wait; the posted set() never ran and
   `done.get()` blocked. The 1 ms poll had kept a timer pending. Fix Detent:3983e723: each job holds
   the owner executor with `outstanding_work.tracked` until it posts set(). Local: the six tests
   pass (0.9-23 s). Proof: mutant coordinator-work (261119-101136-147391, test_delivery_worker_lifetime).
2. relay-enqueue survived (261119-072637-bcfa09) because the script ran plain
   `test_delivery_coordinator`, which never calls relay_service_fixture::qualify; not the health
   turn. Locally, at the mutant, `--subscription-relay-http` fails at the fixture's wake check
   (line 132). Script fixed in Detent:9e698014.
3. A1 `relay never became ready` (261119-072950-ebd34f): harness bug, not a hang. The authority's
   /v2/history/layouts answers 503 history_unavailable for 220-460 ms after the last write, on
   main (224-457 ms, 4 runs) and at the head (254-356 ms, 4 runs) alike; the relay's first sync
   failed and the next came one interval (60 s) later. Fix Detent:26919262: pair.py waits for the
   history before starting the relay; both arms then ready in ~1 s with 0 sync failures.

## Next

1. Read the jobs; record the Evidence rows (A1: interrupt_wakeups_per_s and owner_turns_per_s, F6).
2. Merge main again once rename-detentd lands, then `campaign job rerun` the suite and mutants.

## Design (how it works)

- `OwnerWake` (include/detent/owner_loop.hh), one per `OwnerLoop`: producers `raise()` on any
  thread after their work is visible; the loop calls `begin_turn()` before reading work and
  `arm(timer, deadline)` when nothing can run. A raise after `begin_turn()` either makes `arm()`
  expire at once or posts a cancel that ends the wait, so none is lost.
- Coordinator: each worker job posts `task::event::set()` to the owner; the owner awaits it.
- Relay host turn: runs while it changes anything or a sweep is unfinished, then sleeps until
  min(health 100 ms, snapshot retry) or a wake. Wakes: Service::handle enqueue, reply pump
  writable/settled, `MirrorJob::answered`, every publication-sink call (a `WakingSink` wraps the
  bundle), a coordinator's end, the listener's stop, shutdown, `ServiceRuntime::close()`.
  `ServiceRuntime::step(now, rescan)` sweeps every occupied slot and the session table; a task
  counts as changed when its progress fingerprint moves. The health JSON gains `turns`.
- Authority runtime: wakes at the four enqueues, begin_shutdown/drain, reply pump, storage and
  history driver completions. It sleeps until min(health 250 ms, earliest parked poll or task
  deadline, enabling retry) once nothing is in flight but parked session nexts, as found by a sweep
  since the last change (a wake, a yield turn, or a new applied index). Work in flight keeps the
  1 ms visit; peers configured keep it everywhere (`wake_by`, D124, quest multi-voter-idle-wake).

## Blocked on

Nothing. msg-103 answered (D124, files widened, ruling A); msg-105 answered (mirror_service.hh).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

Messages: msg-103 (widening, ruling A) answered by the lead and done: files widened, multi-voter
keeps 1 ms behind `wake_by` naming multi-voter-idle-wake. msg-105 (mirror_service.hh) answered and
done: `MirrorJob::answered` runs after `done` on both answer paths.

Bench (bench/idle-wakeups): pair.py (detentd + relay fixture), idle_loops.py (A1), paths.py (A3),
compare.sh (build head and baseline, interleave rounds), usage.c. A2 mutants:
tests/detent_delivery/mutants/loop_idle_waits.sh.

Codex's local branch codex/idle-wakeups: usage.c ported from Detent:dbb5029d; its baseline
(docs/perf/idle-wakeups.md at Detent:be0d12b9: 952 interrupt wakeups/s, ~1,004 owner dispatches/s,
reclaim scans ~60 completion ops/s) frames A1.

## Result

<Not yet.>

## Rulings I made

- R1: Worker jobs settle a per-job `task::event` posted to the owner executor, never set on the
  worker: the waiting frame lives on the owner, so set() cannot race its destruction at teardown.
  The pinned task (bdafa5f) has no wheel `arm`, `sleep_until` or `periodic`.
- R2: The wake lives on `OwnerLoop` (one per loop), and the callbacks that reach it from reply
  pumps and mirror jobs are `std::function<void()>`, so distribution_http and the mirror take no
  task dependency.
- R3: Relay: coordinator failure counts, mirror readiness and mirror failure are read at the next
  health turn (at most 100 ms later); they gate a log line, the ready announcement and a shutdown,
  not request service. Session expiry and request deadlines are checked on every sweep, which every
  health turn starts.
- R5: relay-enqueue's covering test is the relay-http mode's wake count (deterministic, no timer),
  not a latency bound against the 100 ms health turn: the survival came from running the wrong
  ctest mode, and only v1 bundle tasks need the enqueue wake (a v2 mirror task is woken by
  `MirrorJob::answered`), which the bench pair does not serve. An end-to-end relay latency test
  stays F3's optional follow-up.
- R4: Authority: transient in-flight work (queued commands in their 5 ms batching window, quorum
  reads, exports, authorization, retirement, W5 frames tasks) keeps the 1 ms visit; only idle and
  parked-only states sleep. Recovery and the stop drain wait on storage completions bounded at
  250 ms. The absence check behind leader trust counts from the end of a planned sleep.
