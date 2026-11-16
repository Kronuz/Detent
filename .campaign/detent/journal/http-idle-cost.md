# http-idle-cost: journal

State: in progress: diagnosing the 100K teardown stall (job 261115-234608-81447d); load, OpenSSL, TSan jobs queued
Quest: .campaign/detent/quests/http-idle-cost.md (the lead's; never edit it)

## Doing

Commits (http): 30cd85f bench, 6f25971 bench OpenSSL header fix + load rows, d92104d E1 TlsStream, f1e173f E2
scratch off the frame, c74162f docs skeleton, 66b85b2 bench load-row fix + phase logging, e336643 suite
OpenSSL include order + version check in tls_asio_test, 261890d AGENTS.md file map.
Product headers (*.h) are identical from f1e173f to e336643: only bench/, test/, CMakeLists.txt, docs changed.

Done jobs at f1e173f: plain10k 261115-214321-2a3f2c, tls10k 261115-214324-a3838f, warm10k (64 KiB POST)
261115-214345-65ed84, watch_ab 261115-214354-19da72 (all noise vs base, A/A geomean 1.000).

Failed at f1e173f:
- 100K tls/plain/owners (261115-214329-4d7b79, -214335-6ff7f4, -214340-d15502): each reached its first
  arm's idle-2 snapshot in 1-3 min (rows in the slot's .scratch/idle/*100k/runs) and then wrote nothing until
  the 10-minute deadline. The driver never reached server.wait (no server_killed_by_driver line), so the
  stall is in the client's close loop or before it. 10K teardown is about 1.8 s.
- load 261115-214349-d25963: `${LOAD_ROWS:-"a b"}` does not field-split, so all rows reached load_ab.py
  as one argument (fixed in 66b85b2; tee hid the exit status, fixed too).

Queued at e336643/261890d: build 261115-234557-843ea7 (slot idle), diagnosis 261115-234608-81447d (100K plain
head, strict, phase timings), suite 261115-234631-7d23fc (done, 13/13), TSan 261115-234650-cd3ef5, OpenSSL
check 261115-234701-d86b6a, OpenSSL mutant 261115-234713-2ebecb (fresh checkout), load A/B
261115-234731-d07ab8 and 261115-234735-ec89ec.

## Next

1. Read the diagnosis; fix the teardown stall; submit 100K one arm per job (base/head x plain/TLS, owners
   separately), each well under 10 minutes.
2. Fill docs/perf/idle.md Numbers (A4): by owner, plain and TLS, 10K and 100K, before/after, A/A bands,
   idle TLS connections per GiB.
3. CHANGELOG entry.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261115-214321-2a3f2c, 261115-214324-a3838f (10K, at f1e173f); 100K pending |
| A2 | 261115-214324-a3838f (TLS 10K), 261115-214354-19da72 (watch_ab noise); load A/B pending |
| A3 | 261115-234631-7d23fc (suite at e336643); TSan 261115-234650-cd3ef5 pending |
| A4 | <pending> |

## Result

<pending>

## Rulings I made

- Codex's baseline (http:becab0a, docs/perf/idle-cost-baseline.md) is input: its owners (Asio staging 2 x 17,408 B,
  BIO pair 34,928 B requested, 64 KiB scratch in the frame) set the cut order; its numbers are re-measured here.
- Header reads go through ConnectionInput::read_ahead (4 KiB) instead of a 64 KiB read: a head over 4 KiB takes
  more reads; bodies still read 64 KiB at a time. The parse buffer is shrunk at the idle wait only past 8 KiB.
- TlsStream does not set SSL_MODE_ENABLE_PARTIAL_WRITE: one async_write_some writes its whole first buffer, so an
  immediate completion never recurses through asio::async_write per record.
- Environment: Apple clang puts /usr/local/include (OpenSSL 4.1 headers) before OpenSSL 3.6's -isystem. The lead
  added CMakeLists.txt to the file set; e336643 puts the found OpenSSL's headers first for tls_asio_test, and the
  test checks headers against the library.
- Bench: bench/idle-cost (server.cc, run.py adapted from Codex's; ab.sh, summarize.py new). The client resets each
  connection (SO_LINGER 0) to avoid the FIN_WAIT backlog Codex hit at 100K.
- The 100K head rows run at e336643, whose product headers equal f1e173f's; the 10K rows stay at f1e173f.
