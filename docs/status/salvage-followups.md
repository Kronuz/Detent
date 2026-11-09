> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning.

# Salvage follow-ups, 2026-11-03

**State:** branch `phase/salvage-followups` (from `github/main` `Detent:5207f96`). Item 3 (docs) is committed; item 2 (one FNV) is in progress; item 1 (port race at the root) is blocked on a shared-library API, see "Blocked on".

## Done

1. Docs match the code: `Detent:955e97d` (`docs/DEPENDENCY_UPGRADES.md`, publication atomicity).
2. One FNV: `PagedSegmentMarks::hash` returns `fnv1ah64::hash` (this branch's FNV commit). `test_local_segment_mark_hash` holds the frozen old loop and four independently computed values; the shared hash equals the old loop on 25,376 identities (22,891 with bytes at or above 0x80), checked before the source switch. A mutant that changes the old loop's prime stops it (`failed: frozen loop matches the independent value`). The hash is the in-memory table slot, not a persisted value. `test_local_paged_segment_marks` and the identity test pass natively (RelWithDebInfo) and under ASan and UBSan (Debug -O1, Apple clang; LeakSanitizer does not run on macOS, the CI Linux jobs run it).
3. CI run 41737319246 at `Detent:0c1aa47` is green on all jobs (freebsd and headers are skipped by the matrix). `macos-x86_64` failed twice on `test_system_managed_poll` and `test_system_managed_authority_failover` ("managed agent 0 did not converge", at 50 to 58 s of a 180 s limit) and passed on the third attempt of the same commit; the other ten jobs passed on the first attempt. The hash change is value-identical, and `linux-arm64-gcc` failed once on the first push because the new test's signed-byte control used plain `char` (fixed in `Detent:0c1aa47`).

## Next

1. Item 1 after the library owners add `port()` to `reactor::TcpServer` and `http::HttpAsioService`; the retry wrapper `Detent:b1c8422` stays until then.

## Blocked on

Item 1 (each daemon binds port 0 and reports the port it got). No daemon can report a bound port today, and the smallest report needs an API in two shared libraries:

1. `reactor::TcpServer::start(port)` (pin `fe8c5ce`) binds the acceptors and keeps no record of the local endpoint; `http::HttpAsioService::start(port)` (pin `6a07f99`) forwards to it. Neither exposes the bound port. `authority.log` prints the requested port (`listening with HTTPS on 127.0.0.1:<port>`), which is 0 if 0 is requested. `detent-relay` (`src/detent_relay/host.cc:253`) passes `options.endpoint.port()` the same way and logs none.
2. `detentd` rejects `--port 0` (`PositiveInt`) and names its pid and log files by port (`detentd-<port>.pid`, `src/detentd/main.cc:187,229`); the agent has no listener.
3. The bind-0-then-close pattern is in about 15 places: `test_managed_process.sh` (2), `tests/detent_authority/test_daemon.sh`, `test_multivoter.py`, `tests/detent_system/{authority,managed}.py`, `tests/e2e/detent_e2e.py`, and in-process fixtures that reserve a listener and read `local_endpoint()` (`test_transport_https.cc`, `test_distribution_service.cc`, `test_multi_node_poll_source.cc`, `test_http_connection_aging.cc`, `test_raft_cluster.cc`, `bootstrap_tls_fixture.hh`, `relay_network_fixture.hh`).

Options:

1. Add `unsigned short TcpServer::port() const` (the shared acceptor's `local_endpoint()` after `start`) and `HttpAsioService::port()`, move the reactor and http pins (W2 owns those pins), then add one log line `listening on <address>:<port>` to `detentd --authority` and `detent-relay`, plus `--port-file` if a file is preferred. Recommended: it is the root fix, but it crosses libraries and W2's pins, so it needs the architect.
2. Detentd and relay find their listening socket by scanning their own file descriptors for `SO_ACCEPTCONN` after `start(0)`. No library change, but it is a guess about the library's internals.
3. Keep the retry wrapper (`Detent:b1c8422`).

## Design vs code

1. `docs/LIBRARY_ADOPTION_ARCHITECTURE.md` item 1 says the hashes target is linked explicitly on local publication; the code links it `PRIVATE` on `detent_local_publication`, which is built only with the completion module, so reader-only builds do not fetch it.
