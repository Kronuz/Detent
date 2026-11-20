> Status: DRAFT (2026-11-20), detent-authority adopted at its quest branch head; detent-relay and
> detent-agent not yet (see "Blocked"). Follows cpp-libraries `docs/server-composition.md` at
> cpp-libraries:0579a1a and task:f937bc2 (`lifecycle.hh`, `process.hh`).

# How Detent's mains follow the server composition

The note fixes eight steps for every Kronuz daemon. This page states, per Detent daemon, what its
`main` does at each step, and each place it differs with the reason (Detent D82: a difference in
a value is not a deviation, a different code path is).

## detent-authority (`src/detent_authority/main.cc`)

| Step | What `main` does |
| :---- | :---- |
| Options | `load_config` reads `detent-authority.json`; environment overrides; flags override both. One name per limit, flag and key alike: `--max-clients` (1024), `--max-in-flight` (1024, per reactor), `--idle-timeout-ms` (75000), `--shutdown-timeout-ms` (35000), `--upstream-ca` (the CA `--status` verifies against, else the publisher's). A bad value exits `EX_USAGE`. |
| Process | `run_daemon` and `ProcessLock` from task's `process.hh`, re-exported by `src/detent_cli/daemon.hh`. The lock is taken inside `run`, after the fork. |
| Signals | `task::stop_signals`, the first object after the fork. The first SIGINT or SIGTERM stops, the second hurries, the third flushes and dies. |
| Limits and descriptors | `max_clients` and `max_in_flight` go to http's constructor, the idle timeout and the 5 s grace to its setters. |
| TLS | `reactor::tls::configure` for the publisher, as before. |
| Loop and components | An `asio_scheduler` on the main thread runs the lifecycle. Components in order: `AuthorityComponent` (stop: `begin_shutdown`; drain: `Service::drain`), `HttpComponent` (start: bind and log the listening line; drain: http's stop), `LeaderComponent` (start: a lone quorum waits for its own leader). |
| `lifecycle` | `task::lifecycle(loop, signals, {shutdown_timeout, min(5 s, bound)}, ready, ...)`. `ready` is `run_daemon`'s callback: it reports readiness to a waiting parent. A stop before ready under `--daemonize` reports "stopped before startup completed"; a runtime failure before a leader reaches the parent with its cause. After the loop returns, `main` closes and destroys every scheduler's roots with every `io_context` alive (D15). A stop that leaves unfinished work exits `EX_TEMPFAIL`. |
| Housekeeping | None in `main`. |

### Deviations that remain

| Deviation | Reason | Ends with |
| :---- | :---- | :---- |
| The authority's owner loop keeps its own thread; `main`'s loop only runs the lifecycle | `OwnerLoop` (`include/detent/owner_loop.hh`) has no `run()`; the note's Settled 4 adds one | the owner-loop quest |
| `LeaderComponent` waits on a 5 ms `sleep_for` | `Service` has no wake when it learns a leader (`service.cc` is loop-idle-waits' file) | loop-idle-waits: `Service::leader_known` event |
| Bounds by setters (`HttpAsioService` takes `max_in_flight` and `max_clients` as constructor arguments) | the pinned http predates `Limits` | the http and reactor pins |
| No descriptor check | the pinned reactor has no `raise_open_files` | the reactor pin |
| `--upstream-ca` only feeds `--status` | the authority dials no one over client TLS (voters use `peer_tls`) | not a deviation (note, C9) |
| `AuthorityComponent::drain` blocks the control loop | `Service::drain` is synchronous; the stop bound is enforced by http's grace and the owner waits' truthful end at `begin_shutdown` | loop-idle-waits, or an async drain |

## Test

`tests/detent_authority/test_daemon_sigterm.sh BINARY [BOUND_MS]` starts the daemon, runs eight
clients of health requests, sends SIGTERM, and requires exit 0 within the bound with no response
cut short.

## Blocked

detent-relay and detent-agent run their loops, signals and stops in `src/detent_relay/host.cc` and
`src/detent_agent/managed_host.cc`, outside this quest's file set; D125's single config file needs
`src/detent_authority/service.cc` (`load_service_options`), `src/detent_relay/configuration.cc`
and `src/detent_agent/config.cc`. The journal lists them.
