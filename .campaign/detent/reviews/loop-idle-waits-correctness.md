# loop-idle-waits review: correctness

Commit: Detent:f5cab9d91e69a4187707575f26fefe04b72b3eff (code; the branch head Detent:d433ed9a adds bench and journal only)
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

No lost wake can hang a loop that sleeps on a timer. Every relay sleep ends by its 100 ms health
turn, and every single-voter authority sleep by its 250 ms health report. A wake the code misses
therefore costs at most that much latency. Only the coordinator's event waits have no bound, and
every worker job sets its event. The approval covers the code. It does not count as evidence for
A1-A4: those jobs were all still queued when I wrote this, and the lead reads them before landing.

## Evidence I relied on

None yet. Every job behind A1-A4 was still queued: 261119-072616-b3ced4 (suite),
261119-072624-8d06ce, 261119-072631-37fc18, 261119-072637-bcfa09, 261119-072644-c03c0d and
261119-072651-75ab72 (the A2 mutants), 261119-072950-ebd34f (A1) and 261119-073003-062077 (A3).
CI run 44555535342 (TSan and the platforms, at Detent:21abab78) was still in progress. Its code is
the same as f5cab9d9.

## What I checked myself

1. **OwnerWake protocol** (include/detent/owner_loop.hh:30-48). A raise that comes after
   begin_turn() either shows up at arm() or posts a cancel to the owner. That cancel cannot run
   until the turn's handler returns, so it ends the wait. arm() and the async_wait always run in
   one owner handler:
   1. Relay: host.cc:383-384.
   2. Authority: `sleep()`. task's child await is a synchronous transfer, and `await_asio` calls
      its initiation inside `await_suspend` (task_asio.hh at the task pin bdafa5f).

   So there is no check-then-sleep window. A stale cancel costs one extra turn, never a lost one.
2. **Producers.**
   1. Authority: the four enqueues (service.cc:1012, 1077, 1137, 1191), stream_task's pump
      callbacks, the storage and history driver completions, begin_shutdown and drain. The parked
      sweep is redone after every wake and every applied-index change. A session next's poll and
      task deadlines and enabling_at bound the sleep. Single-voter peer input does not exist
      (`peers` is null).
   2. Relay: the Service::handle slot, the reply pump, `MirrorJob::answered` (both answer paths),
      every WakingSink call, coordinator end, listener stop, shutdown and close(). A slot release
      counts as progress (service.cc:410), so a task blocked on the transfer count is revisited.
   3. Coordinator: all five worker sites post set() on every path.

   Accepted latencies (R3 and R4): relay request deadlines, poll deadlines and session expiry are
   up to 100 ms late, and authority session expiry is up to 250 ms late.
3. **Teardown (R1).**
   1. The coordinator's set() runs only as a handler on the owner's io. The owner is stopped
      before destroy_roots, and a destroyed waiter unlinks itself from its event.
   2. Relay: release() runs before io.stop() (host.cc:469). `tick` is destroyed after the owner
      thread joins. The mirror, listener and runtime all go before `owner`.
   3. Authority: ReleaseWake is declared after `timer`, so it clears `sleeper` first, including
      when destroy_roots destroys the frame. detentd destroys the HTTP service before the
      authority, so no pump callback posts into a destroyed io_context.
4. **Multi-voter.** `peers` is set before the first sleep (service.cc:2031) and never reset.
   `transient` includes `peers`, and `wake_by` caps every sleep at 1 ms: startup, the main loop and
   the stop drain. The multi-voter cadence is unchanged and cannot sleep forever.
5. **Mutants.**
   1. owner-wake (hour-long sleeps; 5 s and 30 s waits) and coordinator-disk (a real hang, ended
      by ctest's 120 s timeout) fail only on a true hang.
   2. relay-enqueue and reply-writable are deterministic call counts. No other wake fires between
      their before and after reads: stream creation calls no on_writable at http
      Detent-pinned c813531.
   3. authority-enqueue is statistical. Without the wake, about 2.4 of 10 reads are prompt; the
      test needs 8.
6. **A1 and A3 commits.** The head Detent:21abab78 has the same code as f5cab9d9. The baseline
   Detent:dc213daa has the same src and include trees as main and as the merge base
   Detent:be6f4177.

Probe jobs, never waited on; read them when they finish. The scripts are under
`.scratch/detent/review-loop-idle-waits-correctness/` (`probe.sh`, `leader_probe.cc`; the
CMake hooks add the probe without editing sources):

- 261119-074708-891273, at f5cab9d9. It measures time to leader for a single voter, 15 runs.
  It also runs test_authority_idle_wake 10 times alone and in 3 rounds of 4 concurrent copies, and
  exits 1 if any of the 22 runs fails.
- 261119-074710-bd91ce, at dc213daa: the same time-to-leader probe on main, for F1.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | A single voter's election deadline does not end a sleep. At consensus a197efba, a lone voter campaigns at the first Tick past its election delay. An idle owner sleeps until its 250 ms health report, so detentd's first leader, and the lone-quorum readiness wait, can come up to 250 ms later than on main. The delay is bounded and happens only at startup. | service.cc:3259, 3276-3283; consensus test/consensus_test.cc:71; probes 261119-074708-891273 vs 261119-074710-bd91ce | Count `!leader_known` as transient (the old 1 ms visits until a leader is known), or bound `until` by origin plus the seeded election delay. |
| F2 | no | defect | The relay turn does not count a snapshot install as progress. `runtime->step` runs before the bundle visits, and `progress \|= !complete && ...` is false on the step that installs. A next waiting on `offer()` Busy for a snapshot, visited earlier in that turn, therefore waits for the next wake or health turn, up to 100 ms. Snapshot-mode sessions normally take the bootstrap plan, so I did not confirm that this path is reachable. | host.cc:395, 428-432; service.cc:323; update_cache.cc:63 | `progress \|= after.snapshots != before.snapshots`, so the next turn rescans. |
| F3 | no | gap | No relay test runs host.cc's sleeping turn. relay-enqueue proves that a callback is called, not that the loop serves within one turn. Several wakes have no test at all: mirror answered, WakingSink, listener stop and coordinator end. Each lost wake costs at most 100 ms, so none can hang. A3's relay v2 path (paths.py) is the only end-to-end check, and it covers enqueue and mirror answered. | relay_service_fixture.hh (check wakes > idle_wakes); host.cc:376-490 | Optional: a relay counterpart of test_idle_wake against the pair fixture. |
| F4 | no | gap | test_authority_idle_wake passes on latency: 8 of 10 reads within 60 ms. The mutant separates cleanly, but nothing yet shows the threshold holds on a loaded or TSan host. | test_idle_wake.cc; probe 261119-074708-891273; CI 44555535342 linux-tsan | If either fails, assert on owner turns between the read and its answer instead of on wall time, or use more reads with the same ratio. |
| F5 | no | nit | Each worker posts set() outside its own try. If that post throws (bad_alloc), the owner awaits forever; the old poll would have seen `ready`. | coordinator.cc:249, 316, 447, 716, 797 | Post inside a noexcept path, or terminate on failure, as the scheduler-failure branch does. |
| F6 | no | nit | A1's `owner_turns_per_s` includes the sweep yields after each wake. The "no wakeups" claim should be read from `interrupt_wakeups_per_s`. | idle_loops.py | Report both in the Evidence row. |

## Not run

- TSan, ASan and the platform builds: CI 44555535342 runs them, and they had not finished.
- The suite, the five mutants, A1 and A3: queued as the contributor's jobs, not rerun here.
- A test proving F2 is reachable, or that the relay serves a request within one turn of
  host.cc (F3).
- detent-agent's managed host: its 20 ms `watch` timer is outside the quest's files.
- Multi-voter behavior beyond reading the code: that belongs to quest multi-voter-idle-wake.
