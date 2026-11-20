# loop-idle-waits: journal

State: waiting on jobs 261119-103506-5b36b1 (suite), 261119-103509-644510 261119-103509-1f64d3 261119-103509-c4f26f 261119-103509-3937ff 261119-103509-5a6c35 261119-103510-241774 261119-103519-3b65cd (A2 mutants), 261119-103617-426e37 (A2 plain), 261119-153343-7b0799 261119-153345-8528aa (A1 at 60000 and 250 ms), 261119-103510-0db86e (A3), 261119-103541-50a978 261119-103542-42bdbc 261119-103543-da90b6 261119-103544-4f2621 (test_relay_service under TSan, 4x5 runs); CI 44589259502 at Detent:cc942c46
Quest: .campaign/detent/quests/loop-idle-waits.md (the lead's; never edit it)

## Doing

Fix round 2 (review CHANGES at Detent:30391330; the last round before a re-plan). Jobs queued at
Detent:cc942c46.

1. F7 (blocking): a multi-voter authority missed a stop that landed mid-pass. The absence check
   measured from max(pass start, end of the 1 ms sleep planned after it), so a stalled pass never
   counted. The local suite at Detent:30391330 failed the same way (261119-101104-bee765:
   test_relay_service a5, "voter 2, knowing no leader, answered /v2/history/layouts 200").
   Fix Detent:d7d4193f: `owner_absence(pass, slept, planned, next)` in owner_loop.hh sums the pass
   (up to the sleep's start) and the lateness past the planned deadline; the planned sleep never
   counts. test_owner_absence covers stop mid-pass, stop in the sleep, a long planned sleep, an
   early wake, no sleep, a split stall and a deadline already past. Mutant absence-merged restores
   the merged rule (261119-103519-3b65cd). TSan loop of test_relay_service: 4 jobs x 5 runs.
2. F8: test_coordinator_teardown joins the worker pool in a guard declared after the owner, so a
   failing require() cannot leave a job posting to a freed io_context (Detent:91379253).
3. F9: A1 runs at interval_ms 60000 (stated in the Evidence row); compare.sh takes
   IDLE_INTERVAL_MS, and 261119-153345-8528aa adds the 250 ms default (Detent:cc942c46).

## Next

1. Read the jobs; record the Evidence rows (A1: interrupt_wakeups_per_s and owner_turns_per_s, F6,
   at interval_ms 60000 and 250). Read CI 44589259502's linux-tsan test_relay_service and
   test_authority_idle_wake (F4).
2. rename-detentd had not landed at 3d0571cd; merge main (never rebase) once it does, then
   `campaign job rerun` the suite and mutants.

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
| A1 | <queued: 261119-153343-7b0799 (relay interval_ms 60000), 261119-153345-8528aa (interval_ms 250, the default)> |
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
  250 ms.
- R6 (F7): the absence behind leader trust is the pass plus any lateness past the planned sleep's
  deadline, summed (`owner_absence`). The review asked for either part alone at election_min; the
  sum catches both of those and a stall split across them, and is never larger than main's rule
  (the whole gap), so it marks a return only where main would. A planned sleep never counts (R4).
- R7 (F8): the teardown guard has no fails-without test: only a failing require() reaches the path,
  and only ASan would witness the freed io_context.
