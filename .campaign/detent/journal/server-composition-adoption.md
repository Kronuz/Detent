# server-composition-adoption: journal

State: waiting on jobs 261121-133118-09b34d (build all + unit/daemon/secrets ctest), 261121-133128-18e429 (system: relay, agent and authority SIGTERM, dual stack, managed relay and process), 261121-133143-d866a0 (authority-only and relay-only builds, docs check), 261121-131314-98a5a7 (authority at 26a96e4a); CI at 247d1717 (A3)
Quest: .campaign/detent/quests/server-composition-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Authority (26a96e4a and before): composed through task::lifecycle, sigterm test, docs. Round 2 (rulings
msg: pin kept as (a); file set widened except service.cc and owner_loop.hh): detent-relay and detent-agent
composed the same way (host.cc and managed_host.cc: StateComponent/ListenerComponent/ReadyComponent,
CoordinatorComponent/ReadyComponent; `task::stop_signals`; `signals.fail` for mirror and coordinator
failures; abandon reported as unfinished, exit EX_TEMPFAIL), D125 for relay and agent
(`detent-relay.json` + `secrets` file; `agent.json` renamed `detent-agent.json`, `managed_path` is its
secrets file), tests for each, docs/mechanisms/server-composition.md complete.

## Next

1. Read the jobs listed in State; fix failures (a fix gets a test that fails without it).
2. After pin-wave and multi-voter-idle-wake land: merge main, drop the task pin line, apply
   `.scratch/server-composition/d125-authority-service.patch` (service.cc loader: process fields skipped,
   `secrets` file for the credentials; unverified, not compiled) and finish the authority half of D125
   (main.cc `--authority` gone, every test that starts an authority passes `--config`).

## Blocked on

Nothing new; the lead's rulings stand. Still true:
- pin-check is red until pin-wave (the one-line task pin is the branch's, per ruling 1).
- The authority half of D125 waits on service.cc (multi-voter-idle-wake holds it); with it, `--authority`
  is replaced by `--config` in every test that starts an authority (authority.py, tests/detent_authority/*,
  bench) and `authority.json` fixtures split their credentials into a secrets file.
- Files changed outside the widened set, each mechanical and named for the lead: `src/detent_relay/host.hh`,
  `src/detent_agent/managed_host.hh` (their `run_host`/`run_managed` signatures), `src/detent_cli/config.*`
  (in set), `tests/detent_relay/test_relay_config.cc` and `test_relay_service.py`,
  `tests/detent_system/{managed,test_dual_stack,first_read_gate,run_trust_negatives}.py`, the new tests,
  `tests/detent_delivery/test_managed_process.sh`, `tests/detent_agent/{test_agent_cli.cmake,test_reader_capacity.cc}`,
  `tools/w5/first_read.py`, `bench/idle-wakeups/pair.py` (relay and agent file names and the relay's
  split), `packaging/**` (config file names), `README.md`, `QUICKSTART.md`, `docs/{RELAY,USAGE,ACTORS,SERVICES,LOGGING,READER_CAPACITY_ARCHITECTURE}.md`,
  `docs/diagrams/actors-where.d2`, `website/src/content/docs/{operations,architecture,guides}/*` (file
  names and the relay's capacity key). Historical docs (`docs/measurements`, `docs/status`, `docs/reviews`,
  `docs/dev`) still say `agent.json`/`relay.json`.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/mechanisms/server-composition.md (the three mains' tables, deviations with owners, tests) |
| A2 | authority: 261121-131314-98a5a7 (26a96e4a) and test_authority_daemon_sigterm in 261121-133118-09b34d; relay and agent: test_system_relay_sigterm and test_system_agent_sigterm in 261121-133128-18e429 |
| A3 | ci:macos-arm64 at 247d1717; 261121-133143-d866a0 (authority-only and relay-only builds, docs check); pin-check red until pin-wave |
| A4 | jobs 261121-133118-09b34d, 261121-133128-18e429. relay: test_relay_config (secrets mode, link, missing, token in main file refused) and test_system_relay_sigterm (starts from detent-relay.json plus secrets); agent: existing managed-process and CLI tests on detent-agent.json plus managed.json (private-file rule); authority: not done (Blocked) |

## Result

Not ready for review.

## Rulings I made

- Authority exit codes: a bad option value exits EX_USAGE (note step 1); other errors still exit 1.
- Defaults max_clients 1024, max_in_flight 1024, idle_timeout_ms 75000 (the note's Settled 5): the authority gains a 75 s idle close it did not have.
- The listening line is logged when http starts, not at ready, because the existing daemon tests read the port from it while the parent still waits.
- 26a96e4a: detent_cli links task::asio and drops its empty daemon.cc (cause of the CI Build failures); documented the new flags on the website reference pages.
- Kept the 5 ms leader wait as a `sleep_for` inside `LeaderComponent::start`, recorded as a deviation ending with loop-idle-waits.
