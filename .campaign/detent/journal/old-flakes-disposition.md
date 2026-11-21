# old-flakes-disposition: journal

State: waiting on jobs 261121-152400-2cea7c 261121-153510-7f1905 261121-153523-37854b 261121-153524-08d011 261121-153526-d94ed9 261121-153527-140682 261121-153555-ce8385 261121-153557-8e000d 261121-153558-fea7be 261121-153600-ce6a7a 261121-153606-ffac65
Quest: .campaign/detent/quests/old-flakes-disposition.md (the lead's; never edit it)

## Doing

Dispositions (draft, results pending in the Jobs table):
- Flake 3 (`managed.py` "agent did not converge", macos-x86_64): `test_system_managed_poll` is gone (e7d603a7, 2026-11-20, ported the cohort to W5/W6 sessions and removed the poll mode); failover and partition measured at main.
- Flake 4 (`test_transport_https.cc:76`, ASan): `reply->publish()` straight after `reply->start()`. http's OwnedStreamExchange answers `Busy` while the header write is in flight (`writing_`), and `start()` wakes the loop to write it (http_owned_stream.h `publish`, `take_write`, `complete_write`). The test never waited for `on_writable`. Fixed in the test (5010c15a); forcing widens the window to 200 ms and pauses the producer 20 ms.
- Flakes 5 and 8 (`test_local_paged_publication_artifacts`, StoragePressure, plain and TSan): fixed by ci-test-flakes (`Detent:9b85fdf6`, landed 5ebea120): the builder's Work owner retired on the RetirementRuntime thread after the pressure admission. Re-measured at main.
- Catch-up segfault under `-j`: reproduction attempt at main (ctest -j4, 8 rounds); timeouts were ci-test-flakes' causes 10-11.
- A4: the `agent-empty` wait of the relay isolated test is gone (30c2d65b removed `test_relay_isolated_process`); `test_managed_process.sh`'s remaining waits now share `wait_until`, which names the awaited condition, process liveness and `agent --status` with its exit on timeout (58e519e4).

## Findings from the ended jobs (resume)

- 261121-132218-06a35a, -132220-9aaee5 (flake 4 forced) and -131902-fd8099 (TSan 5/8) never ran their tests: `cp`/`tsan_configure.sh` write under `.scratch/`, absent in a fresh slot. No evidence either way; resubmitted with `mkdir -p .scratch`.
- 261121-132019-4bf380 (A4 forced) failed by a bad mutation: `sed s/knob_count=0/knob_count=9/` without `g` changed only the description in the `wait_until` line, the pattern kept `knob_count=0`, the test passed. Resubmitted with `g`.
- 261121-131855-20dfc1 (flake 3 partition x50 at main): 1 of 50. The failing run: agents opened at voter 1 (`managed_initial_source follower=1 leader=0`), the partition isolated voter 1 (`old_leader=1`): the leader moved onto the voter the agents were parked on between `ManagedCohort.start` and the partition; agent 0 stayed at position 1 of 5 for the 30 s. Forcing probe 261121-152400-2cea7c (agents opened at the leader that is then isolated).

## Next

0. Resume plan: read 2cea7c (partition forced). If the agents never leave the isolated voter, name it: the leader moving onto the agents' source voter is a fixture assumption; fix is `run_partition.py` choosing/pinning `old` away from `cohort.opened_at` (not in my file set: write under Blocked on) unless the agent should fail over (product: service.cc / coordinator, not mine). Then rerun failover and partition x50 at the final commit.
1. Read the jobs below; set each flake's disposition from them.
2. Write the dispositions in docs/DEPENDENCY_UPGRADES.md and docs/status/http-coroutines.md; run the tests/docs check.
3. Push the branch once a round of code is done and only jobs remain.

## Blocked on

Nothing.

## Jobs

At c7d4921a (main's code):
- flake 3: failover x50 4-way 261121-131835-3ce856; partition x50 261121-131855-20dfc1
- flake 4: plain x200 8-way 261121-131857-fb61db; ASan/UBSan x200 261121-131900-0e051e
- flakes 5, 8: plain x200 261121-131858-1e3d23; TSan x100 261121-131902-fd8099
- catch-up segfault: ctest -j4 x8 rounds 261121-131908-86e23b
At 58e519e4 (A4): forced 261121-132019-4bf380; mutant 261121-132020-e89451; x20 at the fix 261121-132022-8ee8f6
At 5010c15a (flake 4 forcing): fixed 261121-132218-06a35a; mutant 261121-132220-9aaee5

At 87f69a56 (resume reruns; the 4 harness failures fixed with `mkdir -p .scratch`, `g` in the sed): flake 4 plain 7f1905, ASan 37854b... see ids in State; submitter script: ~/.scratch/detent/ofd/submit.py.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending: docs/DEPENDENCY_UPGRADES.md |
| A4 | pending |

## Result

<pending>

## Rulings I made

1. Flake 4's fix is in the test: `Busy` from `publish()` while the header write is in flight is the stream's documented backpressure answer, not a product fault.
2. The baseline soaks run at main's code (c7d4921a differs from main only in campaign files).
