# loop-idle-waits: journal

State: waiting on jobs 261119-072616-b3ced4 (suite), 261119-072624-8d06ce 261119-072631-37fc18 261119-072637-bcfa09 261119-072644-c03c0d 261119-072651-75ab72 (A2 mutants), 261119-072950-ebd34f (A1), 261119-073003-062077 (A3); CI run 44555535342 (TSan)
Quest: .campaign/detent/quests/loop-idle-waits.md (the lead's; never edit it)

## Doing

Jobs queued at Detent:f5cab9d9 (suite, mutants) and Detent:21abab78 (A1, A3 rows, baseline
main Detent:dc213daa). Local check at f5cab9d9: detentd, detent-relay and all tests build clean;
test_owner_wake, test_authority_idle_wake, test_distribution_reply_pump and
test_delivery_coordinator pass. Bench smoke (debug-ish local build, relay sync 100 ms): idle detentd
55.5 owner turns/s and relay 13.4 turns/s, mostly the relay's 10/s syncs; paths.py runs both paths.

## Next

1. Read the jobs: each mutant must fail its test; A1 head turns near the deadlines (authority 4/s
   health, relay 10/s health) against main's ~1,000/s; A3 head within 5% of main on req/s and p99.
   `campaign job rerun` the suite and mutants at the head if the bench commits made them stale.
2. Merge main once rename-detentd lands (it shares tests/detent_authority and tests/detent_relay).
3. TSan is CI's (ci.yml `tsan: true`): read run 44555535342 for A4.

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
- R4: Authority: transient in-flight work (queued commands in their 5 ms batching window, quorum
  reads, exports, authorization, retirement, W5 frames tasks) keeps the 1 ms visit; only idle and
  parked-only states sleep. Recovery and the stop drain wait on storage completions bounded at
  250 ms. The absence check behind leader trust counts from the end of a planned sleep.
