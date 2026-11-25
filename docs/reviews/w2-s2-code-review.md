> Status: CURRENT (2026-11-03) code review of W2 S2-S5, branch `phase/w2-s2-series` at `Detent:c1d0ba9` (reviewed on `review/w2-s2`).

# W2 S2-S5 code review

**Verdict: APPROVE WITH CONDITIONS.** The owner hop, the stop wiring, the owned-stream reply and D15's teardown hold up under every probe I wrote, in ASan/UBSan and TSan, on the real transport as well as the harness. One blocker: the series does not contain the test fix the brief says it does, and the test it fixes fails 4 runs in 10.

Host: macOS x86_64, Homebrew LLVM 23.1.2, Debug `-O1 -g`, separate `build-asan` (`address,undefined`) and `build-tsan` trees under `quiet light`. Pins match the brief (`cmake/dependencies.json`: http_lib `b18e115`, reactor `950a375`, http_log `3b49b60`).

## Blocking

**CR1. The series lacks `Detent:60e0fa9`; `test_authority_service` is flaky on it.** `git merge-base --is-ancestor 60e0fa9 HEAD` is false. `git diff HEAD phase/w2-s2 -- src include tests` shows exactly two files, `tests/detent_authority/test_service.cc` and `tests/support/handler_harness.hh`: the series carries the pre-fix reader loop (`test_service.cc:524`) while `src/` is identical. CI run 41719588818 ran `phase/w2-s2`, so it does not cover this tree.

Repro: `.scratch/test_service_series-asan` (the series test plus a diagnostic at `:527`), 10 runs: 4 fail. Each failure is `reader k ... status=503 outcome_unknown after ~9.6 s`, then abort. `ctest -R test_authority_service --repeat until-fail:3` in `build-asan` failed on the second pass (17.7 s against 9 s for a pass). The same file taken from `phase/w2-s2` (`.scratch/test_service_fixed-asan`): 10 of 10 pass.

Cause (item 4: a test bug, not a product race). Sixteen `Async` readers start on their own threads and reach the owner in any order. The owner grants reply credit first come, first served, and a granted reply holds its credit until the test releases it. The old loop calls `readers[i].done()` in start order, so a reader that has no credit yet blocks the test, which is the very thing that would release the credit, until that reader's backstop (`timeout` 10 s + 1 s) answers `outcome_unknown`. The product behaved as designed: FIFO credit, truthful 503 at the backstop. The fix at `60e0fa9` serves readers in the order they finish and removes the hold-and-wait.

Fix: cherry-pick `60e0fa9` onto the series (test-only), re-run CI on that head.

## Non-blocking

**CR2. Authority probes the developer reported missing all pass; add them as tests.** `.scratch/probe_authority.cc` (harness) and `.scratch/probe_server.cc` (real `HttpAsioService`, raw socket):

- keep_alive: `request.keep_alive` true gives `Connection: keep-alive`, false gives `Connection: close`, for a command and a read. On the wire, two pipelined keep-alive reads return two replies and the socket serves a third request; a `Connection: close` request is answered with `close`.
- Over budget (`reply_bytes` 1024, a 326-byte read held): a command answers `503 outcome_unknown` (`service.cc:1011`) though it committed. The same sequence re-sent after the release returns 200, and `reserved_reply_bytes` returns to 0.
- T11: the write deadline handed to the stream is `outcome_deadline + 1 s`. After the handler returns it is 3997 ms away with a 3000 ms timeout, so at least 1 s remains (`service.cc:1027`).
- Pass in ASan and TSan.

**CR3. T6, `stopping` versus `outcome_unknown` (review L3): correct, and conservative.** `probe_authority race`: a transaction races `begin_shutdown()` at a random 0-400 ms delay, then the service restarts on the same directory and `/v2/result` is read. 40 runs in ASan: 23 `200`, 17 `outcome_unknown`; 25 in TSan: 8 and 17; 0 violations (a `200` that did not commit, or a `stopping` that did). `stopping` never appears, because `create_extension` refuses after `begin_shutdown()` (`service.cc:671`) and the window before `enqueue` (`:832`) is a few microseconds. At 0-40 ms, 4 of 40 runs answered `outcome_unknown` for commands that had not committed: the queue drain at `service.cc:2768` answers `outcome_unknown` for tasks the runtime never started. This is truthful and replay-safe. If the owner wants `stopping` there it is a one-line change, not a fix.

**CR4. Teardown (D15, T13): no finding.** `probe_server shutdown|forced|nobegin` builds main.cc's sequence on a real `HttpAsioService` with a read blocked on reply credit: `begin_shutdown`, `stop`, `drain`, owner `stop`, `close` on both, `destroy_roots` on both. A blocked client gets a 503 in 2-3 ms; `forced` (grace 0) the same; `nobegin` (no `begin_shutdown`, no `drain`, the wait outlives the loops) leaves `~Service` without a hang. `live_frames()` returns to baseline and `orphaned_waiters()` is 0 in every run: ASan x15 `shutdown`, 1 each of `forced` and `nobegin`; TSan x5 `shutdown` and `forced`. This closes authority T13. `main.cc:333-343` follows `docs/mechanisms/teardown.md` (stop, close, destroy on all loops; `app` destroyed after `service`). `OwnerLoop` members destroy in the right order (thread, guard, scheduler, `io_context`).

**CR5. Affinity (item 3): covered only by real-transport tests.** `Writer` in `tests/support/handler_harness.hh` has no reactor, so http's `affine()` (`http_asio.h:279`, `!reactor_ || reactor_->on_loop()`) never fires in the harness tests, including `test_service`. The assertion did hold in `probe_server` (Debug, asserts on) for the buffered shutdown reply and the owned stream after the hop, and in `test_distributor_owner` for classic. The authority has no committed test on the real transport for the hop; `probe_server` is that test. No `assert_off_loop` fired in any run. The parse and JSON validation stay on the loop (`service.cc:722`, 256 KiB cap, D6/C2): CPU, no blocking.

**CR6. Pins and shim (item 5): clean.** T12 and both T10 greps are empty. `git grep` for `await_suspend`, `awaitable`, adapter and bridge finds only `include/detent_transport/await_io.hh:26`, which wraps `task::await_asio` (task's own awaiter), not an `asio::awaitable` adapter. `--workers` is refused by name (`src/detent_cli/config.cc:117`), also through `DETENTD_WORKERS`; `--workers=N` falls to the unknown-option error. Removing `--workers` is the owner's ruling.

**CR7. TSan on the fixed test (item 4).** `.scratch/test_service_fixed-tsan`: 200 of 200 pass, and no log has a ThreadSanitizer report. The fix removes the flake; it was not a data race. Every other suite in the table passes in TSan x3 with no report.

## Evidence

| Run | Result |
|---|---|
| ASan/UBSan `ctest -R 'test_owner_wait\|test_authority_service\|..._ownership\|..._transport_https\|test_distributor_owner\|test_distributor_basic\|teardown'` x3 | all pass except `test_authority_service` on repeat 2 (CR1) |
| TSan, same set x3 | 8 of 8, no report |
| Series `test_service` x10, ASan | 6 pass, 4 fail (CR1) |
| `phase/w2-s2` `test_service` x10, ASan | 10 pass |
| `phase/w2-s2` `test_service` x200, TSan | 200 pass, 0 reports |
| `probe_authority` race, ASan 40 and TSan 25 | 0 violations (CR3) |
| `probe_server` shutdown/forced/nobegin/keepalive, ASan and TSan | clean (CR2, CR4) |

Probes and harnesses, all kept in `.scratch/`: `probe_authority.cc`, `probe_server.cc`, `probe_build.sh`, `mac.sh`, `test_service_series.cc`, `test_service_fixed.cc`, `support/handler_harness.hh` (the fixed copy), `run1.sh` to `run8.sh`, `tsan_fixed_*.log`, and their `.out` files.

## Conditions

1. CR1: bring `60e0fa9` into the series and show CI on that head.
2. CR2: port the keep_alive, over-budget and `probe_server` cases into the committed tests (a follow-up commit; the code needs no change).

Not run: Linux, FreeBSD and arm64 (left to CI); the classic clustered stop (classic is going away: its stop reply says "retry" for a command raft may have committed); T4's 1025-client row, T7, T1(e), T3(b), the authority T6 queued-in-consensus rows and the commit-time measurements (listed open); a mutant per claim (the existing suites' mutants were not repeated); `-O2` or Release builds.
