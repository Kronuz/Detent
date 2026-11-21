# old-flakes-disposition: journal

State: waiting on jobs (baseline soaks at c7d4921a, forcing and A4 jobs at 5010c15a)
Quest: .campaign/detent/quests/old-flakes-disposition.md (the lead's; never edit it)

## Doing

Dispositions (draft, results pending in the Jobs table):
- Flake 3 (`managed.py` "agent did not converge", macos-x86_64): `test_system_managed_poll` is gone (e7d603a7, 2026-11-20, ported the cohort to W5/W6 sessions and removed the poll mode); failover and partition measured at main.
- Flake 4 (`test_transport_https.cc:76`, ASan): `reply->publish()` straight after `reply->start()`. http's OwnedStreamExchange answers `Busy` while the header write is in flight (`writing_`), and `start()` wakes the loop to write it (http_owned_stream.h `publish`, `take_write`, `complete_write`). The test never waited for `on_writable`. Fixed in the test (5010c15a); forcing widens the window to 200 ms and pauses the producer 20 ms.
- Flakes 5 and 8 (`test_local_paged_publication_artifacts`, StoragePressure, plain and TSan): fixed by ci-test-flakes (`Detent:9b85fdf6`, landed 5ebea120): the builder's Work owner retired on the RetirementRuntime thread after the pressure admission. Re-measured at main.
- Catch-up segfault under `-j`: reproduction attempt at main (ctest -j4, 8 rounds); timeouts were ci-test-flakes' causes 10-11.
- A4: the `agent-empty` wait of the relay isolated test is gone (30c2d65b removed `test_relay_isolated_process`); `test_managed_process.sh`'s remaining waits now share `wait_until`, which names the awaited condition, process liveness and `agent --status` with its exit on timeout (58e519e4).

## Next

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
