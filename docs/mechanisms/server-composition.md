> Status: DRAFT (2026-11-21), detent-authority, detent-relay and detent-agent adopted at the quest
> branch head, except the authority's config merge (see "Remaining"). Follows cpp-libraries `docs/server-composition.md` at
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

## detent-relay (`src/detent_relay/main.cc`, `host.cc`, `configuration.cc`)

| Step | What `main` does |
| :---- | :---- |
| Options | `load_config(args, "detent-relay.json", ..., role_fields)` reads the process settings (`log_level`, `verbosity`, `color`, `runtime_dir`, `max_clients`, `max_in_flight`, `idle_timeout_ms`, `shutdown_timeout_ms`) from the one file that also holds the role settings; `load_host_options` reads the role's and skips the process ones. Flags override the file. The limits have the authority's names and defaults, except `max_clients` and `max_in_flight` default to the role file's admission bound (1024). A bad value exits `EX_USAGE`. `secrets` names the one private file (no group or other access; refused otherwise) that holds `upstream_token` and the downstream `credentials`. |
| Process | `run_daemon` and `ProcessLock` from `daemon.hh`; the lock is taken inside `run`, after the fork. |
| Signals | `task::stop_signals`, the first object after the fork (the same one-two-three stop, hurry, flush). |
| Limits and descriptors | `max_clients` and `max_in_flight` to http's constructor, the idle timeout and the 5 s grace to its setters. |
| TLS | `http::tls_connection_factory` from the role file's certificate, as before. |
| Loop and components | The control loop (`asio_scheduler`) runs the lifecycle on the main thread; the relay's owner loop keeps its own thread. Components in order: `StateComponent` (stop: `shutdown` on the owner thread, which closes the runtime and the mirror; drain: the owner's turn found every request drained), `ListenerComponent` (start: bind; drain: http's stop, then tells the owner the listener is done), `ReadyComponent` (start: waits for the owner's turn to announce the mirror ready, or for the stop). |
| `lifecycle` | As the authority's. A mirror failure or a turn that throws goes to `signals.fail`, which stops with that cause. A stop before ready under `--daemonize` reports "stopped before startup completed". An owner that does not drain by the bound's abandon is reported unfinished: exit `EX_TEMPFAIL`. |
| Housekeeping | The owner's turn (requests, health, mirror readiness) keeps its own timer and wake. |

## detent-agent (`src/detent_agent/main.cc`, `managed_host.cc`, `config.cc`)

| Step | What `main` does |
| :---- | :---- |
| Options | `load_config(args, "detent-agent.json")` as before, now with `shutdown_timeout_ms` (`--shutdown-timeout-ms`, default 35000). `managed_path` names the private secrets file (`managed.json`) that `load_managed_options` already reads under the private-file rule. |
| Process | `run_daemon` and `ProcessLock`; the lock is taken inside `run`. |
| Signals | `task::stop_signals`, the first object after the fork. |
| Limits and descriptors | None: the agent serves no listener. |
| TLS | The managed source's, from the secrets file. |
| Loop and components | The agent's single `asio_scheduler` loop runs the lifecycle. Components: `CoordinatorComponent` (start: spawns the coordinator's `run` and the readiness monitor; stop: `coordinator->close()`; drain: the coordinator's drain, or the bound's abandon), `ReadyComponent` (start: waits for the monitor to announce readiness, or for the stop). |
| `lifecycle` | As the authority's. A coordinator that fails goes to `signals.fail`. |
| Housekeeping | `monitor`'s 20 ms status loop (logging and the readiness check). |

### Deviations that remain

| Deviation | Reason | Ends with |
| :---- | :---- | :---- |
| The relay's `ListenerComponent::drain` blocks the control loop (http's `stop` is synchronous) | as the authority's | an async http stop |
| The relay and the authority keep their owner loop on its own thread | `OwnerLoop` has no `run()` (the note's Settled 4; `include/detent/owner_loop.hh`, held by multi-voter-idle-wake) | the owner-loop change |
| The agent's `monitor` polls the coordinator every 20 ms | `Coordinator` has no status wake (loop-idle-waits) | a coordinator status event |
| The relay has no `--upstream-ca` and no `raise_open_files` | the relay's upstream CA is the role file's `mirror.ca`; the pinned reactor has no `raise_open_files` | the reactor pin |
| Process settings have no environment overrides | the relay and agent had none | |
| `max_clients` for the relay is set by flag, file, or the role file's default | one name; the role file's range 1 to 65,536 applies | |

## Tests

`tests/detent_authority/test_daemon_sigterm.sh BINARY [BOUND_MS]` starts the authority, runs eight
clients of health requests, sends SIGTERM, and requires exit 0 within the bound with no response
cut short. `tests/detent_system/test_relay_sigterm.py AUTHORITY RELAY TLS_FIXTURE [BOUND_MS]` does
the same to a relay over TLS. `tests/detent_system/run_agent_sigterm.py` stops an agent under a
stream of rewrites, requires exit 0 within the bound, restarts it on the store it left and has the
independent oracle accept every value. `tests/detent_relay/test_relay_config.cc` covers the
single file and the secrets rule: a secrets file with group or other access, a link or no file is
refused, a configuration others can write is refused, and the upstream token in the configuration is
refused.

## Remaining

The authority's half of D125 (`detent-authority.json` carrying the role settings of `authority.json`,
the credentials in a secrets file, `--authority` gone) changes `src/detent_authority/service.cc`
(`load_service_options`) and every test that starts an authority. The loader's change is a patch in
the journal's Result; it applies after multi-voter-idle-wake lands.
