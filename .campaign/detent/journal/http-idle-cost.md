# http-idle-cost: journal

State: blocked: A2 not met at http:ad6771f after experiment 4 (c1 CPU/req and TLS close req/s just outside
their A/A bands; Blocked on). Waiting on TSan flake probes 261116-104619-51831f (ad6771f), 261116-104630-0feb00 (main)
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

A2 fix results, jobs at http:47d6726 (resubmitted by the lead; the 0517xx ids were malformed, exit 127):
build 261116-055924-00f72e; load TLS c1/c64 base/defer/head/A/A 261116-055927-af0541; TLS 1 MiB, TLS close,
plain 1 MiB 261116-055931-6dcd9b; 10K TLS 261116-055934-9fc4ec; counts 261116-055939-c54ec0; suite
261116-055945-264897; TSan 261116-055954-8acab6; 100K TLS ABBA base0 261116-060000-d56d24, head0
261116-060007-067b1b, head1 261116-060012-60b2e7, base1 261116-060018-27237b, owners 261116-060022-c30d0a; join
mutant 261116-051730-1f186a. All exit 0.
- D77 paired (median of 5 per-round ratios vs base, [min-max] = 93.8% distribution-free interval):
  | row | arm | req/s | CPU/req | A/A req/s | A/A CPU |
  |---|---|---:|---:|---:|---:|
  | TLS / c1 keep | defer 74d5d7b | 0.939 [0.860-0.966] | 1.143 [1.120-1.251] | 0.995 [0.967-1.028] | 1.014 [0.977-1.032] |
  | TLS / c1 keep | head 47d6726 | 0.921 [0.885-0.992] | 1.143 [1.073-1.189] | | |
  | TLS / c64 keep | defer | 0.791 [0.754-0.798] | 1.286 [1.254-1.327] | 1.028 [0.975-1.093] | 0.973 [0.915-1.025] |
  | TLS / c64 keep | head | 0.821 [0.791-0.872] | 1.218 [1.148-1.264] | | |
  | TLS 1 MiB c8 | head | 1.102 [1.035-1.131] | 0.933 [0.907-0.994] | 0.978 [0.942-1.038] | 1.023 [0.962-1.062] |
  | TLS / c8 close | head | 0.992 [0.945-1.044] | 1.007 [0.947-1.050] | 1.013 [0.995-1.101] | 0.964 [0.953-1.004] |
  | plain 1 MiB c8 | head | 1.006 [0.967-1.039] | 0.994 [0.962-1.034] | 0.999 [0.983-1.021] | 1.001 [0.979-1.018] |
  Absolute CPU/req: c1 27.58 base, 32.41 defer, 30.79 head us; c64 16.12, 20.84, 19.43 us.
  Read-ahead's share, head/defer paired: c1 req/s 1.002 [0.974-1.056], CPU 0.976 [0.939-1.007] (noise); c64
  req/s 1.036 [1.029-1.114], CPU 0.949 [0.897-0.972] (5 of 5 rounds better, median inside the A/A band).
  Defer's share: none measurable. The counts show it removed no syscall, and its arm sits where 76f698d sat
  (c64 0.791x here vs 0.760x in 261116-012337-d0d5f6, a different job, so not a paired comparison).
- Counts (261116-055939-c54ec0, per request, median of 2): recvfrom c1 2 base / 2 head, c64 2.005 / 2.010
  (read-ahead removed the split-header recv: 76f698d had 3); kevent c1 1.976 / 3, c64 0.019 / 1.020 (unchanged
  from 76f698d's 1.018); io_context execute 0 / 1 (now a defer). sendto 1 / 1.
- The kevent is not the post's. Asio's async_wait always starts non-speculatively (reactive_socket_service_base
  async_wait passes allow_speculative=false), and kqueue_reactor::start_op's non-speculative branch re-arms with
  kevent(EV_ADD|EV_CLEAR) every time the read queue is empty (deps/asio-src .../kqueue_reactor.ipp:263-273).
  TlsStream waits for readability with async_wait after SSL_read's EAGAIN: one kevent per request. Base's
  asio::ssl::stream uses async_receive, which is speculative and registers without a syscall once armed.
- Idle at 47d6726: 10K TLS base 96,680 -> head 39,935 B/conn (-58.7%; spread 101 / 32 B; 76f698d had 39,932),
  per GiB 11,106 -> 26,887 (2.42x). 100K TLS ABBA base 96,447 / 96,494, head 39,790 / 39,803 B/conn: per-round
  ratio 0.4126, 0.4125 (-58.7%), per GiB 11,133 / 11,128 -> 26,985 / 26,976. Read-ahead costs no idle memory
  (+3 B at 10K, +2 to +15 B at 100K vs 76f698d, inside the spread): RELEASE_BUFFERS frees the emptied read
  buffer. Owners at 100K head: OpenSSL 27,909 B requested, heap blocks 9,504 B, 5 task frames, socket mbufs 0.

Experiment 4 (lead's msg: option 1, one experiment), http:ad6771f: a read that wants bytes receives
them with async_receive into the caller's buffer (speculative: no kevent re-arm); the BIO serves a copy held
until OpenSSL takes it, so nothing is held while idle (the pending receive uses ConnectionInput's own 4 KiB
buffer). Handshake, writes and reads under 512 B keep the direct read + async_wait. Local tls_asio_test (light
hold): all pass, 4 MiB upload exercises the leftover path, OpenSSL 29,856 B per idle connection.

Gate passed: count job 261116-081537-cfec36 (build 261116-081525-eb9d17), per request, median of 2:
| per request | c1 base | c1 prev | c1 head | c64 base | c64 prev | c64 head |
|---|---:|---:|---:|---:|---:|---:|
| kevent | 1.998 | 2.995 | 1.999 | 0.020 | 1.021 | 0.021 |
| recvfrom | 2.000 | 2.000 | 2.000 | 2.006 | 2.013 | 2.013 |
| socket async_wait | 0 | 1.000 | 0 | 0 | 1.003 | 0.002 |
| socket async_receive | 1.000 | 0 | 1.000 | 1.006 | 0 | 1.003 |
| io_context execute | 0 | 1.000 | 1.000 | 0.001 | 1.004 | 1.003 |
The kevent re-arm is gone; the deferred write completion (io_context execute) remains, on the private queue.

Experiment 4 jobs at http:ad6771f: load small rows (base, prev 47d6726, head, A/A) 261116-090524-ec6fc0; load
TLS 1 MiB, TLS close, plain 1 MiB (base, head, A/A) 261116-090550-a7dfdc; 10K TLS idle 261116-090554-1c2b25;
suite 261116-090540-ddc83e; TSan 261116-090602-ef2c7e; join mutant 261116-090609-eca92c.

Experiment 4 results at http:ad6771f (all exit 0 but TSan). D77 paired, median of 5 per-round ratios vs base,
[min-max] = 93.8% distribution-free interval; absolute CPU/req base / prev / head:
| row | head req/s | A/A req/s | head CPU/req | A/A CPU | CPU us/req |
|---|---:|---:|---:|---:|---:|
| TLS / c1 keep | 0.935 [0.914-1.056] in | 0.986 [0.915-1.106] | 1.100 [0.997-1.144] OUT | 1.009 [0.910-1.084] | 27.87 / 31.96 / 31.07 |
| TLS / c64 keep | 0.920 [0.910-1.004] in | 0.990 [0.888-1.022] | 1.088 [0.996-1.099] in | 1.011 [0.978-1.124] | 16.48 / 20.01 / 17.42 |
| TLS 1 MiB c8 | 1.138 [1.093-1.222] better | 1.038 [1.024-1.053] | 0.902 [0.844-0.939] better | 0.962 [0.951-0.978] | |
| TLS / c8 close | 0.967 [0.948-0.981] OUT | 0.988 [0.968-1.021] | 1.052 [1.038-1.067] in | 1.032 [0.994-1.053] | 541.5 / - / 562.3 |
| plain 1 MiB c8 | 1.005 [0.997-1.039] in | 1.017 [1.001-1.036] | 0.995 [0.963-1.003] in | 0.983 [0.967-0.998] | |
Load jobs 261116-090524-ec6fc0 (c1, c64: base, prev 47d6726, head, A/A), 261116-090550-a7dfdc (other rows).
This fix's share, head/prev paired: c64 req/s 1.132 [1.111-1.192], CPU 0.883 [0.838-0.900] (-2.6 us); c1 req/s
1.052 [0.956-1.116], CPU 0.933 [0.888-1.020] (-0.9 us). TLS close had no prev arm (47d6726's own job: 0.992x).
10K TLS idle 261116-090554-1c2b25: base 96,608 -> head 40,054 B/conn (-58.5%; spread 100 / 20 B), per GiB 11,114
-> 26,808 (2.41x); +119 B vs 47d6726 (39,935), the receive op's larger handler block (not profiled). 100K not
rerun: 10K moved 0.3%, and A2 is blocked.
Suite 261116-090540-ddc83e 13/13; join mutant 261116-090609-eca92c killed (2 records). TSan 261116-090602-ef2c7e
exit 8: 12/13, tls_asio, readahead, asio passed; coroutine failed on a race in its affinity death test, a
forked child that reads a BodyStream from a foreign thread on purpose (plain socket, serve_connection_body /
channel_service, http_asio.h:1015; coroutine_test does not include http_asio_tls.h, the only file ad6771f
changes). Not in the receive BIO path. TSan at 47d6726 passed the same test, so it reads as a timing flake;
probes run coroutine_test 10x under TSan at main eb08b59 (261116-104630-0feb00) and at ad6771f
(261116-104619-51831f).

## Next

1. Read the two TSan probes: failures at main too = pre-existing, the lead's (report); only at ad6771f = mine.
2. The lead takes the A2 trade to the owner (Blocked on).

## Blocked on

A2 at http:ad6771f, after four experiments (write join, defer, read-ahead, receive BIO). By the rule (a row
regresses when its median lies outside the A/A interval):
- TLS / c1 keep: CPU/req 1.100x [0.997-1.144] vs A/A [0.910-1.084]: outside, +3.2 us/req (31.07 vs 27.87).
  req/s 0.935x is inside its (wide) A/A band [0.915-1.106].
- TLS / c8 close: req/s 0.967x [0.948-0.981] vs A/A [0.968-1.021]: outside by 0.001; CPU 1.052x inside
  [0.994-1.053] by 0.001. At 47d6726 the row was 0.992x; this experiment did not touch the handshake path.
- TLS / c64 keep is inside on both: 0.920x req/s, 1.088x CPU (+0.9 us/req). TLS 1 MiB 1.138x better.
What remains per request vs base (counts, 261116-081537-cfec36): syscalls equal; one deferred io_context op for
the write's immediate completion. Unmeasured per-request costs at head: a malloc + memcpy of the received
bytes, a malloc + copy of the joined reply, the custom BIO. The c1 3.2 us is not apportioned; a profile would.
The trade for the owner: idle TLS -58.5% (40,054 vs 96,608 B/conn at 10K, 2.41x connections per GiB) against
small TLS keep-alive c1 +11% CPU/req (req/s -6.5%, inside noise) and c64 -8% req/s / +9% CPU (inside noise).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Exclusive, results `docs/perf/idle.md` (runs in `docs/perf/idle/`). TLS at http:47d6726 vs http:a17b197: 10K 261116-055934-9fc4ec (3 rounds + owners); 100K ABBA, one arm and round per job, 261116-060000-d56d24, 261116-060007-067b1b, 261116-060012-60b2e7, 261116-060018-27237b; owners 261116-060022-c30d0a. TLS 10K at http:ad6771f 261116-090554-1c2b25. Plain at http:76f698d (plain does not include http_asio_tls.h, the lead's ruling): 10K 261116-012345-cba126; 100K 261116-012410-6d7174, 261116-012414-ba9ee6, 261116-012418-4d5602, 261116-012423-dcb492; owners 261116-012436-2ce3c9, 261116-012441-481714 |
| A2 | NOT MET at http:ad6771f. Idle met: TLS 96,608 -> 40,054 B/conn at 10K (-58.5%) 261116-090554-1c2b25; 100K at 47d6726 -58.7% (jobs as A1). Load 261116-090524-ec6fc0, 261116-090550-a7dfdc (5 rounds, A/A): c1 CPU/req 1.100x vs A/A [0.910-1.084], TLS close req/s 0.967x vs [0.968-1.021] outside; c64 and the 1 MiB rows inside or better. Counts 261116-081537-cfec36. See Blocked on |
| A3 | At http:ad6771f: suite 261116-090540-ddc83e (13/13), join mutant 261116-090609-eca92c (killed: 2 records). TSan 261116-090602-ef2c7e failed in coroutine_test's affinity death test (see Doing; probes 261116-104619-51831f, 261116-104630-0feb00); at 47d6726 TSan 261116-055954-8acab6 13/13. OpenSSL header/library check 261116-000620-05c3f6, 261116-000626-9f041e, mutant 261116-000631-893b28. Idle release/reacquire tests: readahead_test "[I] a request after a long idle is served", "[I] and a body after it gets the scratch back", "[I] after another idle, a body and a request pipelined behind it"; tls_asio_test "a request after a long idle is served on every connection, twice", "an idle TLS connection holds no record or BIO buffers" |
| A4 | `docs/perf/idle.md`: "an idle connection costs 58.7% less at 10K" (load section states the 76f698d regression; not updated, A2 unmet) |

## Result

At http:ad6771f against http:a17b197, i9-9980HK, OpenSSL 3.6.5, exclusive jobs:
- TLS idle: 96,608 -> 40,054 B/conn at 10K (58.5% less), 11,114 -> 26,808 per GiB (2.41x); 100K at 47d6726:
  96,447 -> 39,790 (58.7% less).
- Plain idle (76f698d, unchanged since): 14,327 -> 8,637 B/conn at 100K (39.7% less); 74,945 -> 124,318 per GiB.
- Load: the receive BIO removed the per-request kevent (c64 1.021 -> 0.021/req) and recovered c64 to 0.920x
  req/s / 1.088x CPU (inside A/A); c1 CPU 1.100x and TLS close req/s 0.967x stay just outside; TLS 1 MiB
  1.138x better. A2 not met; docs/perf/idle.md and the CHANGELOG not updated for ad6771f.

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
- The A2 fix's diagnosis was half wrong: the extra recvfrom was the split header (read-ahead removed it), but
  the extra kevent is async_wait's re-arm, not asio::post (defer removed the post's lock path and no kevent).
  Recorded with the Asio source lines; no fourth experiment started without the lead (Blocked on).
- D77 statistic computed per round from load.jsonl with .scratch/http-idle/paired.py (home checkout); its
  numbers equal load_ab.py's cells for the arm/base rows and add the head/defer pairing.
- Experiment 4 (lead's option 1): reads that want bytes receive through async_receive into the caller's buffer;
  reads under 512 B, the handshake and writes keep the direct read + async_wait. The 100K TLS rows were not
  rerun at ad6771f: 10K moved +119 B (0.3%) and A2 is blocked.
