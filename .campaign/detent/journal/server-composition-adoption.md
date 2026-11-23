# server-composition-adoption: journal

State: waiting on jobs 261123-061026-3c4e62 (authority SIGTERM x20), 261123-061028-5a97da (relay_pages x3); CI at ddc4f263 (pushed)
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

00. Round 3. CI at 25458690: macos x2, linux x86_64 clang and gcc, tsan passed (relay SIGTERM fixed). Failed:
   pin-check (task pin, pin-wave's); test_authority_daemon_sigterm on linux asan and arm64-gcc ("1
   responses were cut short": curl 52, an unread request declined by the stop, the relay's cause);
   test_system_relay_pages on asan only ("did not send the delta as pages", relay deltas 0, delta_records
   1 after a 5 s paused rewrite; passed at 820f6a91 and on every other platform, no failure on main).
   ddc4f263 merged main and made the authority test count only curl 18 as torn, and require every
   request before the signal to complete (per-client line counts). 3c4e62 runs it x20; 5a97da runs relay_pages
   x3 locally (Release). Next: read both and the CI at ddc4f263; if relay_pages fails again on asan, run it
   at main's commit in CI terms before touching the relay. The old authority script did not reproduce
   locally (CI only: slow runners), so no --fails-without job for it.
0. Read the jobs in State. 9a4c8d and CI (every platform) failed only test_system_relay_sigterm. Cause: the
   test's classification, not the relay. An instrumented copy of the test on the 820f6a91 binaries found
   every "torn" outcome (14 over 8 runs) was a clean EOF after 0 response bytes, on connections opened at
   most 125 ms before SIGTERM: the stop ends the TLS handshake and the read of a request not yet read
   (Kronuz/http: the session token reaches only those waits), so no request was admitted. None was a
   partial response. c218fe2b counts only a begun-and-unfinished response as torn, fails on any
   unanswered close before the signal, and reports requests sent before and answered after it; 10 of 10
   local runs passed. d3d9f7 runs the old test at the head (passes when it fails).
1. Links: 4ad9fdbf fixed both listed exceptions (splash home's #_top, durable-relay's relative .md link).
2. CI macos-x86_64 also failed test_local_disk_store (line 191, lease collection) at 820f6a91; detent_local
   is untouched here and it passed on macos-arm64. If it recurs, show it at main.
3. After pin-wave and multi-voter-idle-wake land: merge main, drop the task pin line, apply
   `.scratch/server-composition/d125-authority-service.patch` (service.cc loader: process fields skipped,
   `secrets` file for the credentials; unverified, not compiled) and finish the authority half of D125
   (main.cc `--authority` gone, every test that starts an authority passes `--config`).

## Blocked on

- msg (pin-wave ordering): the task bump cannot be reverted. main's pin task:bdafa5f predates
  task::lifecycle (task:919d232) and has neither lifecycle.hh nor process.hh; f937bc2 has both. The
  branch is built on them: `task::lifecycle` in the three mains/hosts (src/detent_authority/main.cc,
  src/detent_relay/host.cc, src/detent_agent/managed_host.cc) and process.hh's run_daemon/ProcessLock
  behind detent_cli. At main's pin it does not compile. Options: (a) pin-wave takes the task bump and
  this quest lands after it, with cmake/dependencies.json handed to pin-wave now (this branch drops
  its line once pin-wave lands); (b) this quest lands first carrying the bump and pin-wave rebases on
  it. Recommend (a), minus the hold: release cmake/dependencies.json to pin-wave, which pins task at
  or after f937bc2. tools/w5/first_read.py is only the agent.json to detent-agent.json rename (2
  lines): kept.

Nothing new; the lead's rulings stand. Still true:
- pin-check is red until pin-wave (the one-line task pin is the branch's, per ruling 1).
- The authority half of D125 waits on service.cc (multi-voter-idle-wake holds it); with it, `--authority`
  is replaced by `--config` in every test that starts an authority (authority.py, tests/detent_authority/*,
  bench) and `authority.json` fixtures split their credentials into a secrets file.
- Widen the file set (none reverted; each follows from D125's file names, the hosts' signatures, the
  A2/A4 tests, or the link fixes): `QUICKSTART.md`, `README.md`, `bench/idle-wakeups/pair.py`,
  `cmake/dependencies.json` (task pin, ruling 1), `docs/{ACTORS,LOGGING,READER_CAPACITY_ARCHITECTURE,RELAY,SERVICES,USAGE}.md`,
  `packaging/**`, `src/detent_agent/managed_host.hh`, `src/detent_relay/host.hh`,
  `tests/detent_agent/{test_agent_cli.cmake,test_reader_capacity.cc,test_secrets_mode.sh}`,
  `tests/detent_authority/test_daemon_sigterm.sh`, `tests/detent_delivery/test_managed_process.sh`,
  `tests/detent_relay/{test_relay_config.cc,test_relay_service.py}`,
  `tests/detent_system/{first_read_gate,managed,run_agent_sigterm,run_trust_negatives,test_dual_stack,test_relay_sigterm}.py`,
  `tests/detent_system/test_agent_sigterm.sh`, `tools/w5/first_read.py`,
  `website/src/content/docs/{architecture,guides,operations}/*`, `website/src/components/PageTitle.astro`,
  `tests/docs/link_exceptions.txt`.
- Earlier list, for the record: `src/detent_relay/host.hh`,
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
| A2 | authority: test_authority_daemon_sigterm in 261122-210356-e066ca; relay: test_system_relay_sigterm in 261122-210358-8be4f9 and x20 in 261122-210409-68087b, old classification fails at the same relay in 261122-210411-d3d9f7 (--fails-without); agent: test_system_agent_sigterm in 261122-210358-8be4f9 |
| A3 | 261122-210359-436a7d (authority-only and relay-only builds, docs check, links with no exceptions left); CI at 4ad9fdbf; pin-check red until pin-wave |
| A4 | relay and agent: 261122-210356-e066ca (test_relay_config: secrets mode, link, missing, token in main file refused; test_agent_cli, test_agent_secrets_mode) and 261122-210358-8be4f9 (relay started from detent-relay.json plus secrets; managed-process on detent-agent.json plus managed.json). authority: not done, waits on service.cc (multi-voter-idle-wake holds it) |

## Result

Not ready for review.

## Rulings I made

- The relay SIGTERM failure was the test's: a connection closed before any response byte after the signal
  is the stop refusing an unread request (Kronuz/http's contract), not a cut response. Kept the http
  contract; a Go-style wait for a fresh connection's first request would be a Kronuz/http change. Noted:
  closing with pipelined requests unread sends RST, which can drop a written reply (lingering close),
  also Kronuz/http's.
- Merge conflict in test_managed_process.sh: took main's wait_until helpers, renamed agent.json to
  detent-agent.json in them.

- Authority exit codes: a bad option value exits EX_USAGE (note step 1); other errors still exit 1.
- Defaults max_clients 1024, max_in_flight 1024, idle_timeout_ms 75000 (the note's Settled 5): the authority gains a 75 s idle close it did not have.
- The listening line is logged when http starts, not at ready, because the existing daemon tests read the port from it while the parent still waits.
- 26a96e4a: detent_cli links task::asio and drops its empty daemon.cc (cause of the CI Build failures); documented the new flags on the website reference pages.
- The authority's half of A4 (`--authority` to `--config`) stays out: it needs service.cc's loader (not mine) and edits tests/detent_authority/* and authority.py outside the file set; the patch under .scratch has it.
- Kept the 5 ms leader wait as a `sleep_for` inside `LeaderComponent::start`, recorded as a deviation ending with loop-idle-waits.
