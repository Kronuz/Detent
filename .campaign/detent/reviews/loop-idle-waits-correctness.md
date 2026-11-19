# loop-idle-waits review: correctness

Commit: Detent:3039133093a2b10b3ccf0b3dbdff470a0082796a (code; the branch head Detent:0fc0688c adds the journal only)
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This is round 2, from a fresh context, after round 1 approved Detent:f5cab9d9 and the suite then hung.
The work-guard fix is right, and it closes the class of loops that run out of work. One blocking
regression remains, and round 1 missed it. Since f5cab9d9, a multi-voter authority's absence check
no longer sees a stop that lands mid-pass (F7). CI's linux-tsan job caught it at Detent:21abab78,
whose authority code is the same as this commit's.

## Evidence I relied on

- 261119-072616-b3ced4 (suite at f5cab9d9, failed). Seven tests timed out:
  test_delivery_worker_lifetime, repair_{delta,unchanged,relay,scrub-worker,forced-again} and
  relay_delta_shutdown_published.
- CI run 44555535342 at Detent:21abab78 (the same src as f5cab9d9).
  1. Every platform timed out in coordinator tests, 4 to 38 per platform, all of them the
     unguarded-run class.
  2. linux-tsan also failed test_relay_service a5: "voter 2, knowing no leader, answered
     /v2/history/layouts 200" (F7).
  3. There was no sanitizer report. test_authority_idle_wake passed everywhere, TSan included (F4).
- A search of the last 40 failed CI runs on any branch (since 2026-11-16). "knowing no leader"
  appears only in 44555535342, on this quest's branch. Main's two latest failures do not include
  test_relay_service.
- Not yet run, so not relied on: the suite at 30391330 (261119-101104-bee765, running), the six A2
  mutants (261119-101125-b77320 through 261119-101136-147391), A1 (261119-101138-d37b48), A3
  (261119-101145-1602c6) and CI at 0fc0688c (44585265812).

## What I checked myself

The scripts are under `.scratch/detent/review-loop-idle-waits-correctness/` (`r2_probe.sh`,
`r2_work_guard.cc`, `r2_absence_model.py`). I ran each one locally before submitting it as a job.

- **261119-102319-246c64, done, exit 0 (guard probe).** It tests the coordinator's exact
  construction, `asio::prefer(any_io_executor{io.get_executor()}, outstanding_work.tracked)`,
  against the pinned asio 1.36.
  1. tracked: run() on an unguarded io_context returned after 204.5 ms, once the job posted, and
     set() had run.
  2. untracked: run() returned after 0.0 ms, under the job, with set() not run. This is the
     suite's hang.
  3. dropped: a job the pool destroyed unrun released the work, and run() returned at 108.6 ms.
  4. copied: 64 copies of the executor all counted and all released.
- **261119-102930-c95b7b, done, exit 0 (absence model, F7).** head leaves `returned`
  unset after a 2.5 s stop mid-pass, where main sets it. The split rule in F7's fix is right in all
  three cases.
- **261119-102322-12a601, queued (the seven tests that hung at f5cab9d9).** It runs each 4 times,
  4 at once, stopping at the first failure. It is queued behind the suite in the quest's slot, so I
  do not rely on it.

### Q1: is that the whole class?

Yes. Only five waits in the quest's code do not register work: the coordinator's `worker_done`
events (coordinator.cc:230, 296, 358, 702, 798). All five capture `owner_executor()`, which is
tracked (185-187). Every other wait still keeps its run() alive:

1. Relay host: its OwnerLoop holds a work guard (owner_loop.hh:68), and every turn ends in a post or
   an armed `tick` (host.cc:476-485). run() returns only at `owner.io().stop()` (470).
2. Authority: on OwnerLoop. Every sleep is a timer wait (service.cc:1480-1486), at startup, in the
   main loop and in the stop drain.
3. detent-agent's managed host (managed_host.cc:199-213): `io.run()` has no guard, but its 20 ms
   watch timer re-arms until `drained()`. A coordinator drains only after its worker job has
   settled.
4. Mirror thread and reply pumps: they are not loops the quest changed. It adds only the `answered`
   and `changed` callbacks, which raise the owner's wake.
5. Test harnesses: run_phase (scripted_upstream.hh:404-461), test_worker and test_coordinator run
   io.run() with no guard. They were the hung tests, and tracked work now covers them. Nothing is
   left unrun when run() returns: it returns only once no job holds work.

### Q2: is the work guard released on every path?

Yes. The guard lives only in the worker lambda's capture and in the set() handler it posts.

1. Normal path: it is released when the lambda ends, after the post is queued, so the count never
   reaches zero in between.
2. Pool stopped or destroyed with the job unrun: the job's destruction releases it (probe:
   dropped).
3. A throw on the worker: each body catches into `result->failure`. The disk job's `work->run()` is
   noexcept (disk_store.hh:32). A throw from the final post reaches asio thread_pool's
   `std::terminate` (thread_pool.ipp:41-44): no hang, and no leaked guard.
4. Cancellation and forced shutdown: `OwnerLoop::stop()` calls `io.stop()`, which ignores the work
   count. Every host and test joins the worker pool before its io_context is destroyed, so no
   tracked executor outlives its context: relay host.cc:526 (`startup` is declared after
   `owner`), managed_host.cc:219, and the tests declare `io` before `worker`.
5. One exception: test_coordinator_teardown's failure path (nit F8).

### Q3: are the two harness fixes honest?

Both are honest, and neither narrows coverage.

1. relay-enqueue now runs `test_delivery_coordinator_subscription-relay-http` (CMakeLists:80-81).
   That mode is the one that calls relay_service_fixture::qualify. Its check
   `wakes > idle_wakes` comes after a sweep that leaves the runtime idle. If the name ever stopped
   matching, ctest would find no tests and exit 0: the mutant would survive, the safe direction.
   The suite still runs every mode. The covering test proves that the callback fires, not that
   host.cc serves the request within a turn (F3, unchanged).
2. A1's `wait_history()` (pair.py:143-155) runs before the relay starts. The sampling window starts
   2 s after both report ready, so the wait does not touch what is measured.
   1. The relay's retry one interval after a failed first sync is pre-existing behavior
      (mirror_service.cc:345).
   2. A1 now measures at `interval_ms` 60000, the configuration's maximum; the default is 250
      (configuration.cc:80). The result records the interval, but the A1 Evidence row must state
      it (F9).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F7 | yes | defect | A multi-voter authority no longer detects a stop that lands mid-pass, so after a stop it trusts frames that waited in its sockets and serves stale history. On main, the absence counts from the previous pass's start (e3e2808e:2066). Here it counts from `max(last_pass, slept_until)` (service.cc:2120), and for peers `slept_until` is the end of a 1 ms sleep planned after the stall (3276, 3285-3289). The stall never registers: `returned` stays unset, the queued frames refresh `leader_contact`, and the voter answers 200 while it knows no leader. Under TSan a pass is long, so a stop usually lands mid-pass. | CI 44555535342 linux-tsan, test_relay_service a5 (`refuses`, test_relay_service.py:609); absent from 40 failed CI runs elsewhere; model 261119-102930-c95b7b (done, exit 0: head misses the stop, main catches it) | Count the two parts apart. The pass part is `sleep_start - last_pass`. The sleep part is `owner_started - planned_until`. Either one at election_min or more sets `returned`. This keeps R4's purpose: a planned 250 ms single-voter sleep is not an absence. Test: factor the rule into a function and unit-test three cases (stop mid-pass, stop in sleep, long planned sleep with no stop). Add a mutant that restores head's rule, and the unit test must kill it. test_relay_service a5 under CI TSan must pass. |
| F8 | no | nit | In test_coordinator_teardown, a failure path (`require` throwing before `worker.join()`) destroys the OwnerLoop while `worker`, declared before it, may still hold a job. That job's tracked executor, and its post, then touch a freed io_context. Only a failing test reaches this path. | test_coordinator_teardown.cc:57, 65, 92-103 | Declare `worker` inside the OwnerLoop's scope after `owner`, or join in a scope guard. |
| F9 | no | gap | A1 runs the relay at `interval_ms` 60000. That honestly isolates each loop's own deadlines, but it does not show an idle relay at its 250 ms default. | idle_loops.py:37; configuration.cc:80 | State the interval in the Evidence row. Optionally add a 250 ms row. |
| F1 | no | defect | Carried from round 1, unchanged: a lone voter's first election can wait for the 250 ms health sleep. | service.cc:3279-3283 | As in round 1. |
| F2 | no | defect | Carried, unchanged: a snapshot install is not counted as relay turn progress (up to 100 ms). | host.cc:428-432 | As in round 1. |
| F3 | no | gap | Carried: no relay test runs host.cc's sleeping turn. Each lost relay wake is bounded by the 100 ms health turn. | relay_service_fixture.hh:129-132 | As in round 1. |
| F4 | no | gap | Partly answered: test_authority_idle_wake passed on all six CI platforms and on linux-tsan in 44555535342. One run is not a flake rate. | CI 44555535342 | Read it again in CI at 0fc0688c. |
| F5 | no | nit | Corrected and closed. A throw from a worker's final post reaches asio thread_pool's `std::terminate` (thread_pool.ipp:41-44), not an endless wait. | asio 1.36 thread_pool.ipp | None. |
| F6 | no | nit | Carried: read A1's "no wakeups" claim from `interrupt_wakeups_per_s`, not from `owner_turns_per_s`. | idle_loops.py | As in round 1. |

## Not run

- The suite, the A2 mutants, A1, A3 and CI at this code: queued or running as the contributor's
  jobs, not rerun here. CI at 0fc0688c will show F7 again only if TSan lands a stop mid-pass.
- 261119-102322-12a601 (the seven formerly hung tests under load): queued. Read it before landing.
- An A/B of test_relay_service a5, head against main, on real binaries: a Release build seldom
  lands a stop mid-pass, so a clean run proves nothing. I relied on the CI failure, the code and the
  model.
- The 21 Linux coordinator timeouts in 44555535342 beyond the seven local ones: by their harnesses
  they are the same unguarded-run class, but only CI at 0fc0688c confirms it.
- The multi-voter wake path itself, which belongs to quest multi-voter-idle-wake.
