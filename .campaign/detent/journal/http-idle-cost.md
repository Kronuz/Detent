# http-idle-cost: journal

State: ready for review (code at 76f698d, docs f21dacf); waiting on jobs 261116-012327-17683d 261116-012332-c66999 261116-012337-d0d5f6 261116-012341-188fac 261116-012345-cba126 261116-012350-66ef55 261116-012355-6cbb4e 261116-012359-7fcd7c 261116-012405-815b06 261116-012410-6d7174 261116-012414-ba9ee6 261116-012418-4d5602 261116-012423-dcb492 261116-012427-2c38f4 261116-012431-fec7c5 261116-012436-2ce3c9 261116-012441-481714 261116-012446-eb31d6 261116-012459-659c51 261116-012504-cf1398
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

TLS load regression (A2), found and fixed:
- Load at 4279a49 (261115-235032-963543, -235036-fc949b): TLS / c1 keep 0.616x base req/s (range 0.589-0.652,
  A/A 0.962), CPU/req 1.676x; c64 0.596x, 1.675x. 1 MiB download 1.116x (better), TLS close c8 0.922x
  (A/A 1.009, ranges overlap), plain 1 MiB 1.005x: noise.
- Local profile (sample, quiet light): head spends ~1.75x base's time in sendto. Cause: TlsStream wrote only
  the first buffer per async_write_some, so a reply's head and body were two records and two sends;
  asio::ssl::stream linearises up to 8 KiB into one SSL_write. 76f698d joins them (operation-owned copy).
  tls_asio_test counts records of a small reply: 1 with the fix, 2 without (local); mutant job
  261116-012504-cf1398.
- Local req/s, TLS c1 keep (light hold, noisy): before the fix base 5,564 / fixed 4,677 / old head 3,508; after
  base 5,454 5,688, join 5,491 5,642, join+SSL_set_read_ahead 5,720 5,573. read_ahead: no measured gain,
  dropped.

Queued at 76f698d (supersedes f3d6be6's): build 261116-012327-17683d; load A/B 261116-012332-c66999,
-012337-d0d5f6; 10K TLS -012341-188fac, plain -012345-cba126 (3 rounds + owners); 100K one arm and round per
job, ABBA: TLS base0 -012350-66ef55, head0 -012355-6cbb4e, head1 -012359-7fcd7c, base1 -012405-815b06; plain
base0 -012410-6d7174, head0 -012414-ba9ee6, head1 -012418-4d5602, base1 -012423-dcb492; owners TLS base
-012427-2c38f4, TLS head -012431-fec7c5, plain base -012436-2ce3c9, plain head -012441-481714. Suite
-012446-eb31d6, TSan -012459-659c51, join mutant -012504-cf1398.

## Next

1. When the jobs land: merge the 100K jobs' out/runs into docs/perf/idle/ and run summarize.py over them
   (rounds 0 and 1 give the spread); replace the 10K rows with 261116-012341-188fac / -012345-cba126 at
   76f698d; fill docs/perf/idle.md's 100K section, connections per GiB, and the load row (A2).
2. If a 100K owners job times out (owners setup is ~4.5x slower), report owners at 10K and 100K head only.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261115-214321-2a3f2c, 261115-214324-a3838f (10K at f1e173f, done); at 76f698d: 10K 261116-012341-188fac, -012345-cba126; 100K 261116-012350-66ef55 .. -012441-481714 (queued) |
| A2 | TLS 10K -58.7% (261115-214324-a3838f); watch_ab noise 261115-214354-19da72; load at 4279a49 found a regression (fixed at 76f698d); load at 76f698d 261116-012332-c66999, -012337-d0d5f6 (queued) |
| A3 | suite 261115-234631-7d23fc and TSan 261115-234650-cd3ef5 at e336643 (passed); OpenSSL 261116-000620-05c3f6, 261116-000626-9f041e, mutant 261116-000631-893b28 (passed); at 76f698d suite 261116-012446-eb31d6, TSan -012459-659c51, join mutant -012504-cf1398 (queued) |
| A4 | docs/perf/idle.md at f21dacf: where the bytes go, what changed, 10K numbers, owners; 100K pending its jobs |

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
- TlsStream joins up to 8 KiB of a buffer sequence into one SSL_write (asio::ssl::stream's linearisation size),
  copying into a buffer the write owns; SSL_set_read_ahead was tried and dropped (no measured gain).
- 10K rows are rerun at 76f698d so every published row names the final product; the after-POST rows stay at
  f1e173f (the write join does not touch idle memory) and are labelled so.
