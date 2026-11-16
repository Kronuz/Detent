# http-idle-cost: journal

State: waiting on jobs 261116-005006-33e605 261116-005016-7c440c 261116-005018-f86639 261116-005020-be4ff3 261116-005022-d66445 261116-005024-5fceda 261116-005025-50df2a 261116-005027-f124b9 261116-005029-e3c253 261116-005031-60857a 261116-005033-e981c8 261116-005035-ed5e67 261116-005037-f0cb2a 261116-005043-9b8768 261116-005050-009870 261115-235032-963543 261115-235036-fc949b
Quest: .campaign/detent/quests/http-idle-cost.md (the lead's; never edit it)

## Doing

Commits (http): 30cd85f bench, 6f25971 bench OpenSSL header fix + load rows, d92104d E1 TlsStream, f1e173f E2
scratch off the frame, c74162f docs skeleton, 66b85b2 bench load-row fix + phase logging, e336643 tls_asio_test
OpenSSL version check (its -I did not take), 261890d AGENTS.md file map, 4279a49 merge main, 8740b29
NO_SYSTEM_FROM_IMPORTED on tls_asio_test, 6ef5cd8 10K numbers + CHANGELOG, f3d6be6 teardown by exit +
FIRST_ROUND. Product headers equal f1e173f's in every later commit except main's http_accept.h (q-values,
not on the idle path).

Done jobs at f1e173f: plain10k 261115-214321-2a3f2c, tls10k 261115-214324-a3838f, warm10k (64 KiB POST)
261115-214345-65ed84, watch_ab 261115-214354-19da72 (all noise vs base, A/A geomean 1.000).

Failed at f1e173f:
- 100K tls/plain/owners (261115-214329-4d7b79, -214335-6ff7f4, -214340-d15502): each reached its first
  arm's idle-2 snapshot in 1-3 min and then spent the rest of its 10 minutes in the client's close loop.
- load 261115-214349-d25963: `${LOAD_ROWS:-"a b"}` does not field-split, so all rows reached load_ab.py
  as one argument (fixed in 66b85b2; tee hid the exit status, fixed too).

Teardown diagnosis (local, quiet light, plain, head binary at f1e173f, .scratch/diag):
- SO_LINGER 0 close loop, 30K: 27.8, 9.5, 1.5 s per 10K; newest first: 35.5, 2.1, 0.3 s; server killed first:
  29.4 s for the first 10K. The cost is each reset close in the client, growing with the sockets still open.
- Server stopped first, then FIN closes: 0.6 s, but 28K FIN_WAIT_2 + 30K LAST_ACK left draining for minutes
  (that residue slowed the next run's setup to 271 s).
- Exit without closing, every socket SO_LINGER 0: 30K in 21 s, 100K in 115 s, no residue. f3d6be6 ends runs
  that way (smoke at 2K: rc 0, no sockets left).

OpenSSL: e336643's -I did not take (clang drops an -I that repeats an -isystem dir; /usr/local/include is
searched before -isystem). 8740b29 sets NO_SYSTEM_FROM_IMPORTED on tls_asio_test. Jobs: OpenSSL 3.6 config
261116-000620-05c3f6 (headers 3.6.5 = library 3.6.5, -I/usr/local/opt/openssl@3/include, libssl.3.dylib),
default config 261116-000626-9f041e (4.1.0 = 4.1.0, libssl.4.dylib), mutant 261116-000631-893b28 passed
(without the property: headers 4.1.0, library 3.6.5, the check FAILED). Pre-fix 261115-235445-d74933 fails the
same way: the mismatch reproduced.

Queued at f3d6be6: build 261116-005006-33e605; 100K, one arm and round per exclusive job (ABBA):
TLS base0 261116-005016-7c440c, head0 -005018-f86639, head1 -005020-be4ff3, base1 -005022-d66445;
plain base0 -005024-5fceda, head0 -005025-50df2a, head1 -005027-f124b9, base1 -005029-e3c253;
owners TLS base -005031-60857a, TLS head -005033-e981c8, plain base -005035-ed5e67, plain head -005037-f0cb2a.
Suite 261116-005043-9b8768, TSan 261116-005050-009870. Load at 4279a49: 261115-235032-963543,
261115-235036-fc949b.

## Next

1. Merge the 100K jobs' out/runs into docs/perf/idle/ and run summarize.py over them (rounds 0 and 1 give
   the spread); fill docs/perf/idle.md's 100K section and the load row (A2).
2. Evidence table; set "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261115-214321-2a3f2c, 261115-214324-a3838f (10K, at f1e173f); 100K pending (jobs above) |
| A2 | 261115-214324-a3838f (TLS 10K), 261115-214354-19da72 (watch_ab noise); load A/B pending |
| A3 | 261115-234631-7d23fc (suite at e336643), 261115-234650-cd3ef5 (TSan at e336643); at f3d6be6 pending |
| A4 | docs/perf/idle.md (10K, owners, watch_ab at 6ef5cd8); 100K pending |

## Result

<pending>

## Rulings I made

- Codex's baseline (http:becab0a, docs/perf/idle-cost-baseline.md) is input: its owners (Asio staging 2 x 17,408 B,
  BIO pair 34,928 B requested, 64 KiB scratch in the frame) set the cut order; its numbers are re-measured here.
- Header reads go through ConnectionInput::read_ahead (4 KiB) instead of a 64 KiB read: a head over 4 KiB takes
  more reads; bodies still read 64 KiB at a time. The parse buffer is shrunk at the idle wait only past 8 KiB.
- TlsStream does not set SSL_MODE_ENABLE_PARTIAL_WRITE: one async_write_some writes its whole first buffer, so an
  immediate completion never recurses through asio::async_write per record.
- OpenSSL: tls_asio_test takes the linked OpenSSL's headers as -I (NO_SYSTEM_FROM_IMPORTED) and checks headers
  against the library at run time, so a mismatch fails the suite instead of passing silently.
- Bench: bench/idle-cost (server.cc, run.py adapted from Codex's; ab.sh, summarize.py new). Runs end by exiting with
  every connection SO_LINGER 0 (the kernel resets them), not by a close loop or FIN closes.
- 100K: one arm and round per exclusive job (the lead's split), ABBA order, two rounds per arm for the spread; the
  head arm is f3d6be6, whose product headers equal f1e173f's but for main's http_accept.h.
- Merged main into the branch (4279a49), as the brief asked.
