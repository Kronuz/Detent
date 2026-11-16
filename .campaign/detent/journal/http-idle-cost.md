# http-idle-cost: journal

State: waiting on jobs 261115-214803-84e90d 261115-214321-2a3f2c 261115-214324-a3838f 261115-214329-4d7b79 261115-214335-6ff7f4 261115-214340-d15502 261115-214345-65ed84 261115-214349-d25963 261115-214354-19da72
Quest: .campaign/detent/quests/http-idle-cost.md (the lead's; never edit it)

## Doing

Commits (http): 30cd85f bench, 6f25971 bench OpenSSL header fix + load rows, d92104d E1 TlsStream, f1e173f E2
scratch off the frame. Local: tls_asio_test and readahead_test pass; 400-conn smoke at head: TLS heap 9.5 KB +
OpenSSL 28.2 KB requested per idle conn (bss_bio.c gone), plain heap 8.5 KB.

Docs skeleton c74162f (docs/perf/idle.md, TRANSPORT.md). Suite 261115-214246-74819f passed at f1e173f (13/13);
TSan 261115-214248-7fc80d passed but did not prove instrumentation, rerun as 261115-214803-84e90d with nm checks.
Jobs: builds 261115-214256-a91ba4 (idle) and
261115-214258-465c9c (watch_ab); exclusive: plain10k 261115-214321-2a3f2c, tls10k 261115-214324-a3838f,
tls100k 261115-214329-4d7b79, plain100k 261115-214335-6ff7f4, owners100k 261115-214340-d15502, warm10k
261115-214345-65ed84, load (wrk TLS) 261115-214349-d25963, watch_ab (existing bench) 261115-214354-19da72.

## Next

1. Read job outputs (`campaign job log`, summary.md/load.txt in each job's out), fill docs/perf/idle.md Numbers
   (A4) with commits, verdicts, conns per GiB; commit raw summaries under docs/perf/idle/.
1a. Rerun `campaign verify http-idle-cost --for A3` at the final head (docs-only commits since f1e173f).
2. CHANGELOG entry; docs/TRANSPORT.md TLS paragraph mentions the stream.
3. If tls_common.c 8,848 B / ml_kem.c 6,336 B per idle conn are worth a further cut: investigate (OpenSSL-owned).

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
- Header reads go through ConnectionInput::read_ahead (4 KiB) instead of a 64 KiB read: a head over 4 KiB takes
  more reads; bodies still read 64 KiB at a time. The parse buffer is shrunk at the idle wait only past 8 KiB.
- TlsStream does not set SSL_MODE_ENABLE_PARTIAL_WRITE: one async_write_some writes its whole first buffer, so an
  immediate completion never recurses through asio::async_write per record.
- AGENTS.md (file map) is outside the file set: the lead may add docs/perf/idle.md and TlsStream to it.
- Environment, pre-existing: Apple clang puts /usr/local/include (OpenSSL 4.1 headers) before OpenSSL 3.6's
  -isystem, so the suite's tls_asio_test compiles against 4.1 headers and links 3.6 (on main too). The bench
  avoids it (6f25971); the suite's CMakeLists is outside the file set.
- Bench: bench/idle-cost (server.cc, run.py adapted from Codex's; ab.sh, summarize.py new). The client resets each
  connection (SO_LINGER 0) to avoid the FIN_WAIT backlog Codex hit at 100K.
