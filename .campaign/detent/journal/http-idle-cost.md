# http-idle-cost: journal

State: waiting on jobs 261116-051657-9fb383 261116-051659-5e726e 261116-051701-9a7b14 261116-051703-05a11f
261116-051705-06d314 261116-051707-2cc499 261116-051709-086f5f 261116-051730-1f186a (A2 fix, lead's option 1, at
http:47d6726); 100K TLS reruns 261116-051828-751348 261116-051830-d5c0df 261116-051832-2c02b3 261116-051834-bb242b
261116-051836-032449
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

Done at 76f698d, all passed (supersede f3d6be6's): build 261116-012327-17683d; load A/B 261116-012332-c66999,
-012337-d0d5f6; 10K TLS -012341-188fac, plain -012345-cba126 (3 rounds + owners); 100K one arm and round per
job, ABBA: TLS base0 -012350-66ef55, head0 -012355-6cbb4e, head1 -012359-7fcd7c, base1 -012405-815b06; plain
base0 -012410-6d7174, head0 -012414-ba9ee6, head1 -012418-4d5602, base1 -012423-dcb492; owners TLS base
-012427-2c38f4, TLS head -012431-fec7c5, plain base -012436-2ce3c9, plain head -012441-481714. Suite
-012446-eb31d6, TSan -012459-659c51, join mutant -012504-cf1398.

A2 fix (lead: option 1, profile first, three experiments at most):
- Profile, exclusive 261116-050534-d3bc76 at e8b4ab3 (http_idle_count: Asio handler tracking counts +
  libidle_syscount interposing recv*/send*/kevent; base a17b197 vs head 76f698d; median of 2 runs, per request):
  | per request | c1 base | c1 head | c64 base | c64 head |
  |---|---:|---:|---:|---:|
  | recvfrom (EAGAIN) | 2 (1) | 3 (1) | 2.002 (1.001) | 3.005 (1.001) |
  | sendto | 1 | 1 | 1.001 | 1.002 |
  | kevent | 2 | 3 | 0.017 | 1.018 |
  | asio::post (io_context execute) | 0 | 1 | 0 | 1.001 |
  | socket handlers invoked | 2 | 1 | 2.003 | 1.002 |
  Cause, measured: (1) the write's immediate result goes through asio::post, which from the loop takes the
  scheduler lock and interrupts the reactor: one kevent per request where base's is a continuation (private
  queue, 0.017 kevent/req at c64); (2) without read-ahead OpenSSL reads the 5-byte header and the body in two
  recvs: one extra recvfrom per request. CPU/req in that (tracked) run: c64 23.1/21.5 vs 17.0/16.7 us.
- Experiment 1, 74d5d7b: asio::defer instead of asio::post (thread-private queue, never inline).
- Experiment 2, 47d6726: + SSL_set_read_ahead(1) (RELEASE_BUFFERS frees the read buffer once empty).
- One load job measures both against base with an A/A arm (the defer arm attributes); the head arm is the
  fresh A/B. Third experiment held in reserve.

## Next

1. Read 261116-051659-5e726e (exp: base, defer 74d5d7b, head 47d6726 = defer + read-ahead, A/A; TLS c1, c64)
   and 261116-051701-9a7b14 (TLS 1 MiB, TLS close, plain 1 MiB): both small-reply rows, req/s and CPU/req,
   inside their A/A bands at head, or name the gap. Attribute defer vs read-ahead from the defer arm.
2. 261116-051703-05a11f: 10K TLS idle at 47d6726 must stay ~39.9 KB/conn. 261116-051705-06d314: counts at
   head should be recvfrom 2/req and kevent ~0.02/req at c64, io_context execute 0.
3. Suite 261116-051707-2cc499, TSan 261116-051709-086f5f, join mutant 261116-051730-1f186a at 47d6726.
4. If green: update docs/perf/idle.md (cause, fix, fresh load table) and CHANGELOG, set ready for review.
   A1 (lead's ruling): plain connections do not include http_asio_tls.h, so the plain 10K and 100K rows at
   76f698d stand. Read-ahead can hold buffered bytes on an idle TLS connection, so the 100K TLS rows rerun at
   47d6726, ABBA after the load jobs: base0 261116-051828-751348, head0 261116-051830-d5c0df, head1
   261116-051832-2c02b3, base1 261116-051834-bb242b, head owners 261116-051836-032449; 10K TLS is
   261116-051703-05a11f.
   If not green and the three experiments are spent: write the measured cause and gap under Blocked on.

## Blocked on

Nothing while the A2 fix jobs run (the lead chose option 1, at most three experiments).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Exclusive, http:76f698d vs http:a17b197, bench 76f698d; results `docs/perf/idle.md` (runs in `docs/perf/idle/`). 10K: TLS 261116-012341-188fac, plain 261116-012345-cba126 (3 rounds + owners). 100K ABBA, one arm and round per job: TLS 261116-012350-66ef55, 261116-012355-6cbb4e, 261116-012359-7fcd7c, 261116-012405-815b06; plain 261116-012410-6d7174, 261116-012414-ba9ee6, 261116-012418-4d5602, 261116-012423-dcb492; owners 261116-012427-2c38f4, 261116-012431-fec7c5, 261116-012436-2ce3c9, 261116-012441-481714 |
| A2 | Idle: TLS 96,742 -> 39,932 B/conn at 10K (-58.7%, spread 152/31 B), 96,527 -> 39,788 at 100K (-58.8%, per-round 0.4123, 0.4121; base A/A 44 B): jobs as A1. Load 261116-012332-c66999, 261116-012337-d0d5f6 (5 rounds, A/A arm): FAILS, TLS keep-alive c1 req/s 0.924x (0.850-0.985) vs A/A (0.963-1.079), c64 0.760x (0.753-0.851) vs A/A (0.941-1.055); TLS 1 MiB 1.137x (better, outside band); TLS close c8 0.989x and plain 1 MiB 0.978x inside band. See Blocked on |
| A3 | Suite 261116-012446-eb31d6 (13/13), TSan 261116-012459-659c51 (13/13, instrumentation asserted), join mutant 261116-012504-cf1398 (2 records without the join, test fails), all at 76f698d; OpenSSL header/library check 261116-000620-05c3f6, 261116-000626-9f041e, mutant 261116-000631-893b28. Idle release/reacquire tests: readahead_test "[I] a request after a long idle is served", "[I] and a body after it gets the scratch back", "[I] after another idle, a body and a request pipelined behind it"; tls_asio_test "a request after a long idle is served on every connection, twice", "an idle TLS connection holds no record or BIO buffers" |
| A4 | `docs/perf/idle.md`: "an idle connection costs 58.7% less at 10K" |

## Result

At http:76f698d against http:a17b197, i9-9980HK, OpenSSL 3.6.5, exclusive jobs (`docs/perf/idle.md`):
- TLS idle: 96,742 -> 39,932 B/conn at 10K, 96,527 -> 39,788 at 100K (58.8% less); 11,124 -> 26,987 per GiB
  (2.42x). Per 100K: 8.99 GiB -> 3.71 GiB, 5.28 GiB saved; the before arm had 5.8-6.1 GB compressed by macOS.
- Plain idle: 14,327 -> 8,637 B/conn at 100K (39.7% less, 14,367 -> 8,661 at 10K); 74,945 -> 124,318 per GiB
  (1.66x); 0.53 GiB saved per 100K.
- Owners: TlsStream removes the 2 x 17,408 B Asio staging and the 34,928 B BIO pair; the 64 KiB read scratch
  left the frame (98,304 -> 2,560 B block). What remains of idle TLS is OpenSSL session state (~28 KB, 70%).
- Wakeups: 0.2/s idle in both arms.
- Load: small TLS replies on keep-alive regress (0.924x c1, 0.760x c64 req/s, outside A/A); 1 MiB TLS 1.137x
  better; TLS close and plain 1 MiB noise. A2 not met.

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
- D77 on the load rows: load_ab.py's cell is already the median of per-round ratios with the min-max over
  rounds, which for five rounds is the distribution-free interval at 93.8% (95% needs six); stated so, no
  rerun. A row counts as regressed when its median lies outside the A/A arm's interval.
- The 10K and 100K runs at 76f698d and both load logs are kept under http docs/perf/idle/ (220 KB) so
  summarize.py reproduces every table; the CHANGELOG's "inside the noise" claim is corrected there.
- Profile tooling: http_idle_count (Asio's ASIO_CUSTOM_HANDLER_TRACKING counting, bench/idle-cost/count_tracking.h)
  and libidle_syscount (DYLD_INSERT_LIBRARIES interposing; recv/send count again as recvfrom/sendto inside
  libsystem, so recvfrom/sendto are the syscall counts). Counts only, never timing.
- Both fixes measured in one exclusive job with a defer-only arm instead of two jobs: same rounds, and the
  arm attributes the gain; read-ahead was dropped earlier on a noisy light-hold run, not an exclusive one.
