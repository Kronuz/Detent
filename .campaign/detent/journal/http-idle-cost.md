# http-idle-cost: journal

State: working
Quest: .campaign/detent/quests/http-idle-cost.md (the lead's; never edit it)

## Doing

E1: replace `asio::ssl::stream` in `serve_tls_connection` with `detail::TlsStream` (http_asio_tls.h): OpenSSL
reads/writes the non-blocking socket through a custom BIO, ops wait for readiness; no Asio staging vectors
(2 x 17,408 B), no BIO pair (2 x 17,408 B), no engine timers. Then tests, then E2.

## Next

1. E2: the 64 KiB `tmp` read scratch leaves the coroutine frame (heap, allocated for a body, released at the idle
   wait); header reads go through ConnectionInput's own read-ahead.
2. Jobs: suite, TSan, idle A/B base/E1/E2 at 10K (exclusive), 100K plain and TLS (exclusive), throughput A/B
   (bench/watch_ab.sh http/httpb, TLS wrk), at the head.
3. docs/perf/idle.md (A4), CHANGELOG.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

<pending>

## Rulings I made

- Codex's baseline (http:becab0a, docs/perf/idle-cost-baseline.md) is input: its owners (Asio staging 2 x 17,408 B,
  BIO pair 34,928 B requested, 64 KiB scratch in the frame) set the cut order; its numbers are re-measured here.
- Bench: bench/idle-cost (server.cc, run.py adapted from Codex's; ab.sh, summarize.py new). The client resets each
  connection (SO_LINGER 0) to avoid the FIN_WAIT backlog Codex hit at 100K.
