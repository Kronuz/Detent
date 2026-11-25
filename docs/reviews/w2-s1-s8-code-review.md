> Status: CURRENT (2026-11-03) Code review of W2 S1, S6, S7 and S8 on `phase/w2-s1` (head `Detent:30df4d9`, main `Detent:cb314a1`).

# W2 S1/S6/S7/S8 code review

Ruling: **APPROVE WITH CONDITIONS** (CR1 before landing; CR2-CR6 are follow-ups).

Reviewed against `docs/design/http-coroutines.md` (revision 3), its reviews, and the ledger `docs/status/http-coroutines.md`. Pins: task `6558c2e`, completion `576618d`, journal `d6d009e`, consensus `666cdfc`.

## Method

Lease box (Ubuntu 24.04, clang 18, Debug `-O1 -g`, 32 cores), separate builds in `~/b/w2-asan` (ASan+UBSan, `-fno-sanitize-recover=undefined`) and `~/b/w2-tsan`, plus `~/b/w2-bench` (benchmarks ON, default). Probes and scripts are in `.scratch/review-w2/`:

- `build.sh`, `rssh.sh`, `buildprobe.sh`: build and run helpers (build dirs stay on the box, outside the synced tree).
- `probe_sweep.cc`, `sweep.sh`: stop-moment sweep of a coordinator (derived from `test_coordinator_teardown.cc`).
- `probe_close.cc`: two `OwnerLoop`s, phase 2 present and removed.
- `jitter_kill.sh`, `jitter_loop.sh`: `test_managed_process.sh` with a SIGTERM of the agent or the relay at a jittered moment while transactions flow.

Environment notes (not findings): on a stock Ubuntu box (umask 002, group-writable `~/b`, `~/w`) 34 of 229 ASan tests fail with "untrusted ancestry"/"requires a trusted regular configuration file" and `FD limit 1024`; with `umask 022`, `go-w` on the ancestors and `ulimit -n 65536` all pass.

## Results

| Build | Run | Result |
|---|---|---|
| ASan/UBSan | full `ctest` (229) | 229 of 229 (after the environment fix above) |
| ASan/UBSan | `test_owner_wait` x30, `test_relay_paged_*_process` x20 each, `test_managed_paged-v2_process` x10 each | all pass (the T13 assert at `host.cc` fires in none) |
| TSan | 68 selected (owner, delivery, teardown, relay, managed, authority_service, system, completion, prepared) + coordinator 100000 set + `test_owner_wait` x30 | all pass, 0 ThreadSanitizer reports; `test_http_socket_ownership` not built (see below) |
| Probe sweep (ASan) | 192 stops: bound-only endpoint (reconnect wait), listening-but-silent endpoint (TLS handshake in flight), each with and without `close()` first, 16 moments from 0 to 250 ms | clean when stopped at or after 6 ms, and every `close()` drains in about 1 ms with the request in flight; the 30 stops between 0.1 and 3 ms without a prior `close()` abort (see CR5) |
| Process jitter (ASan) | 30 SIGTERMs of the agent and 30 of the relay, 150 ms to 3 s, 30 transactions in flight | 60 of 60 exit 0, no sanitizer or assert text in the logs |

TSan link failures: `test_client_publication_failure`, `test_model_alloc_failure`, `test_local_disk_ledger`, `test_local_directory_ownership`, `test_local_client_source`, `test_local_inventory`, `test_http_socket_ownership` fail with "multiple definition of `operator new`" against `libclang_rt.tsan_cxx`. Each defines its own `operator new` (`git grep -l "operator new" -- tests` lists exactly these seven). The branch touches none of them (`git diff main..HEAD --stat -- tests/detent tests/detent_local tests/detent_http src/detent_local src/detent_http` is empty). **Pre-existing, not introduced.**

## Findings

### CR1 (blocking condition): default configure no longer builds

`DETENT_BUILD_BENCHMARKS` defaults ON (`CMakeLists.txt:47`). On the branch, `cmake -S . -B b && cmake --build b --target detent_fleet_load` fails: `benchmarks/fleet_load.cc:216, 329, 337, 487, 488` `co_await` `open_session`/`close_session`/`Coordinator` calls that are now `task::task` ("no matching member function for call to 'await_transform'"). Repro: `cmake -S . -B ~/b/w2-bench -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_TESTS=OFF && cmake --build ~/b/w2-bench --target detent_fleet_load` (log `~/b/bench.build.log`). CI runs with benchmarks OFF, which is why it is green. The ledger records the file as S9 but not that `main` would stop building by default.
Condition: land S9's `fleet_load.cc` move with this wave, or land it before anyone builds `main` with defaults and say so in the landing note. Not a shim or a "both for now".

### CR2 (non-blocking, test gap, not dead code): `OwnerLoop::close()` (D15 phase 2) is observable with more than one loop

The ledger says skipping `close()` has "no observable effect" and survives. That holds for one loop only. `.scratch/review-w2/probe_close.cc`: a frame on loop A whose destructor spawns a child onto loop B, with B's `destroy_roots()` before A's. With `close()` on both: 0 live frames after phase 3. With `PROBE_SKIP_CLOSE=1`: **2 live frames after phase 3** (rc 1). `test_coordinator_teardown.cc` and `test_owner_wait.cc` have one loop each. Fix: add this two-loop case (a mutant that dies) to `test_owner_wait.cc` before S3 introduces the second owner. Keep `close()`.

### CR3 (non-blocking, weaker test): `run_partition.py` no longer shows a stale leader through the write phase

`run_partition.py:88-124`. Before: the isolated leader had to report role 2 through verification, so the scenario exercised a stale leader beside the new one. After: role 2 is required only immediately after isolation (inside `election_max`, deterministic), and afterwards only "reachable and not fenced"; the role is recorded (0 on the box). CheckQuorum makes the old assertion impossible, so the adaptation is correct, but the scenario no longer proves that a stale leader's reads/idle responses/writes are rejected while the new leader serves. Follow-up: a probe that writes straight to `old` after the step-down and requires a not-leader refusal, or a consensus knob to hold CheckQuorum off for this scenario. The single-voter timing in `test_service.cc:62` (20/40/100-200 ms) equals the previous Core defaults, so that adaptation is not weaker. `service.cc:401-405` differs from main only in a comment; its values equal the new Core defaults (`consensus:types.h`: 100/500/1000/2000). Production timing is unchanged.

### CR4 (non-blocking, one shape): two ways to await an Asio operation, one in a Detent-specific wrapper

`include/detent_transport/await_io.hh` adds `detent::transport::await_io`/`checked_io` (throws `system_error`) beside `task::await_asio` (returns the error code), which `coordinator.cc:149,166`, `completion_scheduler.cc` and `client.cc` use directly. Both are on task, with no `asio::awaitable`, so T12 is satisfied, but this is a Detent-local completion adapter beside the library one (done criterion 2: "no Detent completion type"; criterion 3, one shape). Ask the architect whether the throwing form belongs in task (`task::await_io`) so Detent keeps one.

### CR5 (note): a forced stop with a disk original in flight is fail-stop

`probe_sweep` stopped at 0.1-3 ms (store startup I/O in flight, coordinator not closed) aborts in `~LinuxCompletionQueue` (`completion/linux_completion.h:84`, `if (pending_) std::terminate()`), 30 of 192 runs (`rc=134`, "terminate called without an active exception"). This is completion's documented contract, not a leak, and every production path drains before it stops (`run_host` waits `owner_drained`; `run_managed` leaves `io.run()` on settlement). But T13's wording "forced stop (grace 0)" and the ledger's "forced stop leaves no live frame" only hold once storage originals are not in flight. Record that in the design (D15) so S3/S4's forced-stop tests do not stop with a commit's original pending. This answers the "mid-commit" probe: a stop mid-commit is by design a process abort, and a `close()` mid-commit drains.

### CR6 (note): `run_managed`/`run_host` frame check after the io_context

Not a hidden leak. `task_asio.hh:125-128` (`start_timer`): an expiry Asio has queued still holds `on_fire`, which holds a counted block, until the stopped `io_context` is destroyed. `host.cc:409`/`managed_host.cc` now compare `live_frames()` after `run_*_owned` returns. 90 ASan runs of the process fixtures (above) never fail, and the in-phase-3 check stays in `test_coordinator_teardown.cc` (right after `destroy_roots()`, io alive). Note only that the check in the hosts is end-of-function, weaker than inside the run, and compiled out under `NDEBUG`. `scheduler_` is destroyed before `io_` in `OwnerLoop` (and `scheduler` after `io` in `run_managed`), so such a handler's destructor must not touch the scheduler; ASan found nothing, `task_asio.hh` already requires it.

### Items checked, no finding

- Cancellation (risk 2): `network_cancel = task::cancel_source{}` at `coordinator.cc:815` runs on the owner between suspension points, after the `while(!closing)` test, so a `close()` cannot fall between them; the header (`coordinator.hh:70`) says one owner serializes close and cancel. Callers of `close()`/`cancel_update()` in `src` are on the owner (`managed_host.cc:42,53,171`, `host.cc:265`). My first sweep called `close()` from the main thread and saw 8 "did not drain" results: that violated the contract and was an artifact; posted to the owner, all drain in about 1 ms. A cross-thread `close()` would race the renewal; document it as owner-only (header already does). `next_session`/`open_session`/`receive` take the token (`:845,:866,:896`); `close_session` in `release_session` takes none and is bounded by the 1 s timeout.
- T6, `assert_off_loop`, T11, T2, T3 (criteria 4-7): not in this branch (`owner_submit` has no handler caller yet; `assert_off_loop` is S2 by N2, `git grep assert_off_loop` finds only docs). Criteria 1-3, 8, 9 stay unticked, correctly.
- `owner_submit` (`owner_loop.hh:69`): a stop requested before `accept()` still runs `accept()` then the wait ends at once, so `finish` sees the accepted work and can answer `outcome_unknown`; the state is a `shared_ptr`, so a late completion sets an event nobody waits on. `test_owner_wait` T1 (a)-(d) pass in both sanitizers.
- OwnerLoop member order (`io_`, `scheduler_`, `guard_`, `thread_`) starts the thread last; `host.cc` declares `owner` before every object the coordinators reference, so a throw before spawn only runs an idle loop's phases.
- T12 grep over `src include tests benchmarks`: still matches `service.cc` (S4), `test_transport_https.cc` (S2), `benchmarks/fleet_load.cc` (S9); the moved files are clean. `service.cc` runs the completion driver as a task and the rest as `asio::awaitable` on the same `io_context`: a two-shape file until S4, as the design plans.

## Verdict

APPROVE WITH CONDITIONS.

Blocking: CR1 (`benchmarks/fleet_load.cc:216-488`, default build of `detent_fleet_load` fails).
Conditions: CR1 resolved or explicitly sequenced by the lead; CR2 test added before S3.
Follow-ups: CR3, CR4 (architect), CR5 (design wording), CR6.

Not run: macOS and FreeBSD builds (CI), TSan of the 500000-record fixtures, `fleet_load` at runtime, a mutant rebuild of `network_cancel.request()` (the ledger's mutants (1)-(3) were taken as reported; probes above cover `close()` mid-request), Python/Rust bindings, benchmarks.
