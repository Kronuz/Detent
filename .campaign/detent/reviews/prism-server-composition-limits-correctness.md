# prism-server-composition-limits review: correctness

Commit: prism:e0c1ddda87e34077fdc771134ac1ea18506be0a7 (quest/detent/prism-server-composition-limits, merge base prism:a87fa0f)
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A4 suite 261120-035135-f42468 at prism:e0c1ddd: `prism_lifecycle` passed (7.3 s), with the
  drain, limits and tls cases (OpenSSL present, so tls ran; its 14-day warning is in the log).
- A2 mutant 261120-035138-af5e5b at prism:6df5be4, whose only change is `Http::stop()` back to a
  no-op and `drain()` back to a bare `service.stop()` (`git diff e0c1ddd 6df5be4`, src/main.cc
  only). It fails seven drain checks, lifecycle_test.cc:374, :379, :382, :396, :398, :402, :403,
  while load and stalled pass: the drain case is what tells the phased stop from the old one.
- A1 grep job 261120-035136-5bd4e1: no `tls::configure`, `tls_connection_factory`,
  `ConnectionFactory`, setter or raw `asio::ssl::context` left in src/main.cc; one `http::Limits`,
  one `tls::server_context`, one `raise_open_files`.
- CI run 44731002558 at prism:e0c1ddd: freebsd-x86_64, linux gcc and clang, linux-arm64,
  ASan+UBSan and TSan green when I looked; both macOS jobs still running.

## What I checked myself

By reading, against the pins (http aed0314, reactor 6cd8f73, task 0316bef); no probe job: reading
settled every question, so nothing waits.

1. The drain thread (main.cc:435-459). The thread holds `this` (Http), a copy of `b.cancel` and
   the `await_asio` handler. `task::run_lifecycle` awaits `drain_all` to completion and never
   drops a drain frame (task lifecycle.hh:543); `b.abandon` is only a token Prism ignores. So the
   frame resumes only through the posted handler, joins at once (the thread is past its last
   statement), and nothing outlives it. `~Http` joins as a fallback and runs before `io`, `loop`
   and `service` die (declaration order, main.cc:553-556). `stop()` twice: `stop_accepting()` is
   idempotent (reactor.h:881-891) and the lifecycle calls it once. A signal during the drain:
   the hurry requests `b.cancel`; `wait_requests` registers a `cancel_callback` that wakes it under
   the drain mutex and re-checks the token under it (reactor.h:894-906), so no wake is lost, and
   an already-requested token returns at once. A failed thread start throws out of
   `await_suspend` before any handler exists (task_asio.hh asio_awaiter), so no joinable thread is
   left. Posting from the waiter to `io(1)` takes asio's locked queue, as task's own `off_loop`
   does.
2. The bound. `b.cancel` fires at bound - grace; then `cancel_requests()` and `service.stop()`,
   whose grace is `Limits::shutdown_grace` = min(5 s, bound), the same value as `stop_timing.grace`.
   So the stop ends at the bound, plus a second grace only when a forced session still does not
   end (reactor.h:864-867; Prism's handlers are all short). The hurry and the deadline request the
   same token, so the drain test's hurry exercises the wait's wake.
3. Limits (main.cc:534-545): the four options fill one `http::Limits`, built after
   `check_descriptors` shrinks `o.max_clients` (serve now takes `Options` by value). Ranges keep
   the constructor's checks unreachable (idle 0..24 h, grace >= 1 ms, in-flight <= 2^24).
   `max_clients_set` is set on the shared `set_option` path, so flag and JSON key both count.
4. `raise_open_files` (reactor.h:724-746): raises toward the hard limit (and macOS's
   `kern.maxfilesperproc`), ignores a refused `setrlimit` but re-reads and returns the limit in
   force, throws on `getrlimit` failure. Prism turns a short answer into `EX_CONFIG` with the
   numbers (explicit) or a warning with the shrunk value (default), and the limits case checks
   both values ("needs 1088 descriptors", "max_clients is 136" under a hard limit of 200).
5. TLS: `server_context` runs in `serve` before the service and the lifecycle (main.cc:515), so a
   group-readable key, an expired or not-yet-valid certificate and a mismatched pair fail at
   startup, not at a handshake; an unreadable file still fails before the fork (main.cc:609-613).
   The context's `shared_ptr` moves into `ServiceState::tls`, declared before `server_` in the
   service, so it outlives every connection (http_asio.h:1174-1180, :1268-1270). Handshake bounds
   are `Limits`' defaults (5 s, 64), the values the old factory call passed.
6. Shape: ARCHITECTURE.md's rows and the deleted deviation rows match the note's steps 4, 5 and 7
   (cpp-libraries docs/server-composition.md:53-117, C1, C2, C5) and D122; README and `kUsage` say
   the same stop. One mechanism per area; no leftover setter or factory.
7. The port fixture (lifecycle_test.cc:63-77): see F1.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The EADDRINUSE fix is another fixed range, not port 0 read back. `free_port()` probes from 20000 + pid % 20000 upward, closes the probe and passes the number: the bind, close and pass race the proof standards list as a trap and the campaign's fixtures-port-zero shape removes. Its comment's premise, "below the ephemeral range", holds on macOS (49152-65535, checked here) but not on CI's other platforms: Linux's default range starts at 32768 (pids with pid % 20000 >= 12768 land in it) and FreeBSD's at 10000 (every pid). | lifecycle_test.cc:63-77; `sysctl net.inet.ip.portrange.first` = 49152 | Start each prism with `--port 0` and read the port it bound: the banner prints `service.port()` (main.cc:559-560; drop `-q` and use `--no-color` for those spawns, or read RUNTIME_DIR/prism-0.log for the detached case). Delete `free_port()`. The suite passing at the fix confirms it. |
| F2 | no | defect | With `--daemonize`, the new post-fork refusals exit 1, not the documented 78: a config_error in `serve` reaches the parent as the `run_daemon` startup error, a `std::runtime_error`, caught as exit 1. README's "an explicit N that does not fit exits 78" and ARCHITECTURE's Limits and TLS rows state `EX_CONFIG` unconditionally; the message itself is still printed. The TLS half predates this quest (the old `configure` also ran after the fork). | task process.hh:189-199, :225; main.cc:514-515, :620-622 | Owner's call. Options: (a) keep, and say "foreground" in README and ARCHITECTURE; (b) run `check_descriptors` and `make_tls` before the fork (limits and an SSL context inherit; no thread exists yet), at odds with the note's step order; (c) carry the exit status through `run_daemon` (task change). |
| F3 | no | nit | The drain hand-rolls task's `detail::off_loop` (a thread, a post back, a join). Prism is the first `wait_requests` consumer; when Xapiand and Detent adopt it, one public task helper should be the single way. | main.cc:446-455; task lifecycle.hh:451-503 | Follow-up in task, not this quest. |
| F4 | no | nit | An exception from `wait_requests` on the waiter thread calls `std::terminate`. Only a mutex or allocation failure can throw there. | main.cc:450-453 | Catch and post the `error_code` if wanted. |
| F5 | no | nit | The tls case makes a 2-day certificate and the 14-day warning appears in the log, but no check asserts it. | lifecycle_test.cc:447-466; job 261120-035138-af5e5b log | One `slurp(err)` check on a foreground spawn. |
| F6 | no | nit | No test drives the deadline path with a lead (one SIGTERM, bound > grace, a request that never completes); stalled uses bound 2000, lead 0. The wake is the same token as the tested hurry. | lifecycle_test.cc:316-342, :385-405 | Optional: `--shutdown-timeout-ms 6000` with a stuck body, exit in 6 s +- slack. |

## Not run

- No probe jobs: no build, sanitizer or platform run of my own. Linux and FreeBSD ephemeral
  ranges are their documented defaults, not measured on the CI hosts.
- Detached exit status (F2) read from task's `run_daemon`, not run.
- macOS CI jobs of run 44731002558 were still running.
