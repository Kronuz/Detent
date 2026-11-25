# http-idle-cost review: correctness

Commit: http:2d53cef78de262df1417f25560c72e53034dd001 (product code http:ad6771f361a1cfee532301de4e1840b0aa016d4b; 2d53cef adds docs only)
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A3: suite 261116-090540-ddc83e (13/13 at ad6771f); TSan 261116-090602-ef2c7e (12/13, tls_asio,
  readahead, asio, timeouts pass, instrumentation asserted); join mutant 261116-090609-eca92c
  (killed: 2 records). The release/reacquire tests named in the journal exist and assert values:
  readahead_test [A] and [I], tls_asio_test idle/pipelined/4 MiB/close cases.
- A1/A2: the idle and load jobs, accepted as D103 records them; performance is not this lens.

## What I checked myself

The security review's probe (`.scratch/review-http-idle-cost-security/`, `tls_probe.cc` + `run.sh`)
covers this lens's paths too, under ASan+UBSan: head with OpenSSL 4.1.0 261116-111212-cb41da and
3.6.5 261116-111857-ebc534 (tls_probe, tls_asio_test, readahead_test all pass), base eb08b59
261116-111747-1067ba (tls_probe passes). Its cases are listed in the security review.

6. Completion rules (http_asio_tls.h:160-218). Every path that finishes without an asynchronous
   wait or receive passes `phase == start` and goes through `asio::defer` (:201), including an
   empty buffer and an immediate error; every other `finish` runs inside a receive or wait
   handler. Cancellation reaches the inner `async_receive`/`async_wait` through the composed op's
   slot (`task::await_asio` emits `cancellation_type::all`, which passes async_compose's terminal
   filter) and through `lowest_layer().cancel()` from ConnectionInput's `Cancel`. A receive
   completes either with bytes (then `take` copies them before anything else runs, so a cancel
   never loses received bytes) or aborted with none; a deferred result is delivered as success,
   which asio allows. The BIO copy is stream-owned and outlives any operation; the caller's buffer
   is the ConnectionInput's inline 4 KiB (kept alive by the handler's `owner` capture) or the
   frame's scratch, awaited in place; the stream itself is kept by `drain()` as before. Close
   while a receive is pending: aborted, then `finish` without touching the stream (the stop test
   in tls_asio_test). Move: only at handoff after the handshake completed, with nothing in flight.
7. Idle release/reacquire with a read and a write. `respond()` is awaited before the loop returns
   to the head read, so no write is in flight at the idle release (http_asio.h:962-963). The only
   read then is the watch's, into ConnectionInput's own buffer, never the released scratch;
   `read_ahead` arms it or waits on the one already pending. During a reply, a pending watch
   receive and a write interleave on one loop thread; `receiving_` is set only inside one SSL
   call. OpenSSL releases each record buffer only when empty. Probe [W] (KeyUpdate answered while a
   4 MiB write waits) and [C] (input left over past a body into the idle wait) pass.
8. TSan affinity flake: pre-existing and unrelated. Main eb08b59 261116-104630-0feb00 and head
   ad6771f 261116-104619-51831f both fail 5/10 with the same two signatures:
   `channel_service.hpp:563` and `serve_connection_body`'s `stream->push` line (http_asio.h:978 at
   main, :1015 at head; the same statement, shifted 37 lines by this quest's edits above it, the
   statement itself untouched). coroutine_test does not include http_asio_tls.h.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | nit | A write cancelled after `SSL_ERROR_WANT_WRITE` frees its joined copy while OpenSSL keeps the pending record; with `SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER` a later `SSL_write` of other bytes would be credited the old record's length. Unreachable: every failed or cancelled write ends the connection (http_asio.h:1075, :1122). | http_asio_tls.h:73, :115 | none; keep the "a failed write ends the connection" rule if writes ever become retryable |
| C2 | no | nit | ConnectionInput's `Cancel` cancels every operation on the socket (http_asio.h:558), a concurrent write's wait included; pre-existing with asio::ssl::stream, not changed here. | http_asio.h:558 | none in this quest |
| C3 | no | nit | TSan coroutine_test fails 5/10 at main and at head with identical race sites: pre-existing (question 8). | 261116-104630-0feb00, 261116-104619-51831f | out of scope; its own quest if wanted |
| C4 | no | nit | The suite slot now links OpenSSL 4.1.0, the idle numbers were taken on 3.6.5 (as the doc says). Both pass the probes and tls_asio_test's idle bound (29,384 B on 4.1.0, 28,006 B on 3.6.5 per idle connection). | 261116-111212-cb41da, 261116-111857-ebc534 | none |

## Not run

- No new mutants: the quest's one behavior fix (the join) has its mutant; the deferral and the
  idle release are not mutant-tested, and the probes pass at the base too (they guard equivalence,
  they do not discriminate).
- Linux/epoll and CI sanitizers: CI's; no CI run at 2d53cef yet.
- No TSan run of the probe; tls_asio_test's TSan run is the evidence for the stream's threading.
