# prism-server-composition review: correctness

Commit: prism:805acc1141228f95865fa3fdccdf893a339edf7a
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-150720-9d025e (ctest prism_lifecycle at 805acc1, passed): the daemon readiness, pid
  lock and SIGTERM-under-load cases as written in `src/test/lifecycle_test.cc`.
- 261119-150116-cb39e6 (zero-grace mutant at prism:4eb7074, test fails at lines 263-264):
  `src/main.cc` is identical at prism:4eb7074 and prism:805acc1 (`git diff --stat` touches only
  docs), so the kill stands for this commit.
- 261119-150718-559dc2 (build at 805acc1) and CI run 44634843777 at 805acc1, green on every
  platform, with `prism_lifecycle` passing under linux-tsan and linux-asan-ubsan (from the run log).

## What I checked myself

Scripts in `.scratch/detent/review-prism-server-composition-correctness/`, each run against
`build/prism` at 805acc1:

- 261119-153641-291f60 `probe_errors.py`: 25 failure and config cases, exit status and message.
  All match the docs except TLS-when-detached (N1). Missing file and bad JSON exit 78, unknown
  keys and wrong types 64, the old positional `prism 8880` 64 ("unexpected argument"), port in use
  1 with "bind: Address already in use" both in the foreground and detached, a runtime dir that is
  a file 1. `--port` overrides the file's `port` from either side of `--config`. A held lock
  exits 0 with "already running".
- 261119-153642-784109 `probe_stop.py`: shutdown paths the suite leaves out. A stalled peer
  (never reads a 4 MiB response) is cut at the grace: exit 0 after 2.04 s at
  `--shutdown-timeout-ms 2000` and 5.01 s at 6000. Two SIGTERMs 200 ms apart: exit 0 after
  5.01 s, the same as one. Three: killed by SIGTERM after 0.41 s. A POST half uploaded at SIGTERM
  is reset at once: ECONNRESET at 0.3 s, no response bytes. SIGTERM 0-20 ms after spawn kills
  by the default action (before `stop_signals` exists); at 30 ms prism exits 0.
- 261119-153643-0d0583 `mutant_long_grace.sh`: the lifecycle test at a mutant with
  `set_shutdown_grace(60000)`: it SURVIVED (F1).
- Code read: `task::run_daemon`, `ProcessLock` and `locked_pid` (task:0316bef `process.hh`).
  The startup pipe and the lock are CLOEXEC, the fds sit above 2, the lock is taken in the child
  and released last (`serve` declares it first, `src/main.cc:448`). `_Exit(EX_TEMPFAIL)` leaves an
  unlocked pid file, and `locked_pid` reads that as not running. The lifecycle skips stop and drain
  for a failed start, and `reactor::TcpServer::start` cleans up after itself
  (reactor `reactor.h:654-700`). `tls_connection_factory`'s last argument is
  `maximum_connections`, so the plain and TLS bound are the same number. The task pin
  0316bef is on task's origin/main.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The shutdown bound has no test. `std::min(kShutdownGrace, o.shutdown_timeout)` (`src/main.cc:495`) is what keeps a stop within `--shutdown-timeout-ms` when a peer stalls, but the lifecycle test's slow reader finishes in about 0.7 s, so a 60 s grace passes it. A2 claims requests finish within the bound. | 261119-153643-0d0583 (SURVIVED); the code holds the bound today: 261119-153642-784109 (2.04 s at 2000 ms) | Add a stalled-peer case to `lifecycle_test.cc`: a client that never reads a 4 MiB response, `--shutdown-timeout-ms 2000`, SIGTERM; check that prism exits within the bound plus slack (e.g. < 3 s). Rerun `mutant_long_grace.sh` as the `--fails-without` job. |
| F2 | yes | defect | Two stop claims this commit adds are false for Prism. README ("requests in flight finish") and the usage text, README and ARCHITECTURE Signals row ("a second one hurries"). A request whose body is still arriving at SIGTERM is reset at once, with no response. A second signal changes nothing, because `Http::drain` ignores the `stop_bound` and `service.stop()` blocks the loop (`src/main.cc:438-441`). The deviation row names the blocking loop and the at-once cancel, but not these two consequences. | 261119-153642-784109: upload ECONNRESET at 0.3 s; two signals 5.01 s = one signal 5.01 s | Docs only, within the quest's files. Say what holds: a response being written gets the grace, a request still being received is reset, and the second signal has no effect until http has stop phases. Put both in the deviation row that prism-server-composition-limits carries. Rerun `probe_stop.py`. |
| N1 | no | defect | A missing TLS certificate exits 78 in the foreground but 1 with `--daemonize`: `config_error` crosses the startup pipe as a `runtime_error` (`src/main.cc:559-565`). The docs say EX_CONFIG. | 261119-153641-291f60 | Check the TLS files before `run_daemon`, or state "1 when detached". |
| N2 | no | gap | A response cut at the grace exits 0 and reports nothing: `Http::drain` always returns `{}`, so the `EX_TEMPFAIL` branch (`src/main.cc:521-525`) is unreachable for Prism, although the Stop row says otherwise. | 261119-153642-784109 (stalled peer, exit 0) | Say so in the deviation row until reactor reports forced sessions. |
| N3 | no | nit | A second `--daemonize` instance with the lock held exits 0 with nothing on the terminal; "already running" goes to stdout, which is the first instance's log. This is Detent's convention (`detent_authority/main.cc:156`), so it stays the owner's call; one option is `locked_pid` before the fork, for a terminal message. | 261119-153641-291f60 | Owner's call; keeping it is fine. |
| N4 | no | nit | Every `--port 0` instance locks `prism-0.pid`, so a second one in the same runtime dir is refused as "already running on port 0". | 261119-153641-291f60 | Note it next to the port-0 default, or name the lock after the bound port. |
| N5 | no | nit | `BENCHMARKS.md:133` still runs `./build/prism -q 8880`, which now exits 64. | 261119-153641-291f60 (positional port case) | `--port 8880`. |
| N6 | no | nit | An unknown key in prism.json reports "unknown option: KEY" without the file's name, unlike type errors. The `verbosity` key cannot express `-q`. `--color` accepts any value as auto (as before this change). | `src/main.cc:333,330,407-420` | Optional. |
| N7 | no | nit | The load test's `cut` counts only responses that started. A request sent and never answered (`r.started` false) counts as neither, so a server that drops queued requests at the stop would pass. | `src/test/lifecycle_test.cc:233-236` | Optional: count requests sent without a response before `done`. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- TLS serving and shutdown with a real certificate (only the missing-file path).
- Signals that land during the fork itself, or at the waiting parent; read from code only: the
  parent dies by the default action and the child carries on.
- Idle wakeups after the poll's removal. Read from code: the loop waits on `sleep_until(never)`
  and nothing else is scheduled.
- Linux-only behavior locally: the probes ran on macOS. CI covers the suite on Linux and FreeBSD,
  but not these probes.
