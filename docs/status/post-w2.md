# Post-W2 follow-ups, 2026-11-03

**State:** branch `phase/post-w2` at `Detent:393c96a`, CI run 41757189321 green on every job (freebsd and headers skipped by the matrix). Pins moved except cluster and consensus (blocked), fixtures converted except those that cannot start on port 0, CR2 probes ported with mutants.

## Done

1. Pins (`Detent:c500cea`): reactor `950a375` to `a59e9f8`; http_lib `b18e115` to `67622cc` (coordinator's instruction; `99c148d` plus the slow-reader test fix); http_log `3b49b60` to `a6a6ab2`; task `d080786` to `bdafa5f` (what http pins; docs only past `d080786`).
2. Bound ports (`Detent:6683e63`, note `docs/mechanisms/bound-port-report.md`): `detentd --port 0` and relay `"port": 0` accepted; both log the port from `HttpAsioService::port()`. Converted: `test_managed_process.sh` (authority and relay; retry wrapper `Detent:b1c8422` and both probes deleted), `test_daemon.sh`, `test_transport_https.cc` (3 servers), `test_distribution_service.cc`, `bootstrap_tls_fixture.hh`, `relay_network_fixture.hh`, `test_multi_node_poll_source.cc` (its retry loop deleted). `test_relay_config` now expects port 0 to load and 65536 to be rejected.
3. CR2 (`Detent:20b9c29`, `tests/detent_authority/test_service_wire.cc`, `test_authority_service_wire`): keep_alive headers for a command and a read; pipelined keep-alive reads, a third request and a `Connection: close` request on a real `HttpAsioService` with a raw socket; over-budget command answers `503 outcome_unknown`, its replay returns 200 and `reserved_reply_bytes` returns to 0; write deadline margin at least 1 s after the handler.
4. Mutants, each failing its test: `request.keep_alive` replaced by `true` (`keep_alive_headers`); over-budget reply `admission_full` for commands (`over_budget_command`); write deadline from `Clock::now()` instead of `outcome_deadline` (`write_deadline_margin`); daemon logs the requested port (`test_authority_daemon` fails).
5. Native, RelWithDebInfo, authority, relay and managed agent on: `authority|multi_node|managed|relay|bootstrap|poll` 83/83. cluster (bus, raft 2/2) and consensus (transport, completion_asio 9/9) pass at reactor `a59e9f8` in local worktrees.

## Next

1. Owner/lead: publish `post-w2-reactor-pin` in cluster (`cluster:68256da`) and consensus (`consensus:d048373`), both reactor `ade618d` to `a59e9f8`, then pin them here.
2. Publish http-log's pin of http `99c148d` to `67622cc`.

## Blocked on

1. Pushing to cluster, consensus and http-log mains was denied by the permission system (a public surface), so Detent's cluster (`428d57d`), consensus (`666cdfc`) and http_log pins still sit beside libraries that declare reactor `ade618d` (cluster, consensus) and http `99c148d` (http-log). PLAN M1 criterion 1 ("equal to what each declaring library pins") is not met for reactor and http until those land. The CI check the criterion asks for does not exist yet.
2. Not converted, by construction: `test_multivoter.py`, `tests/detent_system/{authority,managed}.py` (three voters name each other's ports before the first start; the managed relays' ports are written into agent configs before the relays start), `test_raft_cluster.cc` (peer ports), and `test_http_connection_aging.cc` (holds its own listener, no race). The relays in `managed.py` could convert; left out for budget.
3. T11 is checked on the harness (the deadline is only observable there); no way to observe the stream's write deadline from a raw socket.

## Design vs code

1. The mechanism note says `detentd` names its pid and log by the requested port; with 0 that is `detentd-0.*`, and `--status` needs the bound port.
