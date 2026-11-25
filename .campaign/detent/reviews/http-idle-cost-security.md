# http-idle-cost review: security

Commit: http:2d53cef78de262df1417f25560c72e53034dd001 (product code http:ad6771f361a1cfee532301de4e1840b0aa016d4b; 2d53cef adds docs only)
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Attacker: a network peer on the TLS (or plain) port, before or after the handshake. Gains sought:
another connection's bytes, a replayed or dropped request, a truncation that reads as a clean
close, memory or CPU out of proportion to what it sends.

## Evidence I relied on

- Suite 261116-090540-ddc83e (13/13 at ad6771f) and TSan 261116-090602-ef2c7e (tls_asio, readahead,
  asio, timeouts pass; only coroutine fails, see the correctness review C3): the existing tests,
  including tls_asio_test's idle reacquire, close_notify and FIN-without-close_notify cases.
- Join mutant 261116-090609-eca92c: the one-record check fails (2 records) without the join.

## What I checked myself

Probe: `.scratch/review-http-idle-cost-security/tls_probe.cc`, run by `run.sh` (rerun:
`PROBE_DIR=<dir> [OSSL_ROOT=/usr/local/opt/openssl@3] [TARGETS=...] sh run.sh` in an http checkout).
It drives the real server through a raw OpenSSL client over memory BIOs, so the test controls
every ciphertext byte and every TCP segment, under ASan+UBSan:

- [F] a request delivered one byte per TCP segment; a 70,000 B body cut at 1/4/511/512/513/4095/
  4096/4097/3/16389 bytes (straddles the 512 B small-read threshold, the 4 KiB watch buffer and a
  full record) echoed byte for byte.
- [R] a record whose header and body arrive across two idle pauses (OpenSSL's read buffer holds a
  partial record across the idle release); a whole record plus a partial one, the rest after an
  idle pause.
- [C] a 100 KB body and 200 one-record pipelined requests coalesced into one send (more ciphertext
  received into the 64 KiB scratch than OpenSSL's read buffer takes, so the BIO copy carries a
  remainder across the body's end and the idle wait): all 200 answered in order, none repeated.
- [K] a client KeyUpdate requesting the server's; [W] the same while the server's 4 MiB write
  waits on a slow reader: the download arrives whole and the connection serves on.
- [N] close_notify followed by a sealed request and junk: the server closes, serves nothing.
- [T] a record cut mid-way, then FIN: closed, unanswered.
- [O] a record header claiming 65,535 bytes; a record with a flipped tag byte: both close.
- [X] 16 concurrent connections (2 reactors), each echoing its own seeded body four times in
  7/300/513/2048 B pieces with idle pauses: 16 of 16 exact, so no connection saw another's bytes.

Jobs: head, OpenSSL 4.1.0 (what the suite slot links): 261116-111212-cb41da, pass (tls_probe,
tls_asio_test, readahead_test, no ASan/UBSan report); head, OpenSSL 3.6.5 (the version the idle
numbers were measured on): 261116-111857-ebc534, pass; base main eb08b59 (tls_probe only, same
behavior expected): 261116-111747-1067ba, pass.

Code read, answering the lead's questions:

1. BIO and read buffers. The BIO serves only `input_` bytes in `[input_begin_, input_end_)`, never
   more than the receive returned (`take`, http_asio_tls.h:221; `bio_read`, :248). A receive
   happens only on `SSL_ERROR_WANT_READ` with `receiving_`, which the BIO returns only once `input_`
   is empty, so `take` never overwrites unconsumed ciphertext. The copy is freed and the cursors
   zeroed the moment OpenSSL takes the last byte; no pool, no state shared between connections
   (the static `BIO_METHOD` is a function table). The caller's buffer is written by the receive and
   then by `SSL_read_ex`, which only reports bytes it wrote. The parse buffer is cleared, not
   reused with content, and released above 8 KiB (http_asio.h:963); the body scratch is fresh per
   body and only `r.bytes` of it are appended. Probe [X] and [C] found no crossed or stale bytes.
2. Read-ahead across idle. OpenSSL releases its read buffer only when empty (`SSL_MODE_RELEASE_BUFFERS`
   with read-ahead), so a partial record or read-ahead bytes survive the idle release; `input_` is
   per stream and is not touched by the idle release; ConnectionInput's 4 KiB buffer is inline.
   Nothing is dropped or served twice: probes [R] and [C].
3. Close. `SSL_ERROR_ZERO_RETURN` is `eof`; a transport EOF without close_notify is
   `stream_truncated` (http_asio_tls.h:145, :152), as asio's engine maps them; the server's rules
   treat both as the end of the connection, as before. The server sends no close_notify, which is
   unchanged (the base never called `async_shutdown`). An idle-released connection reallocates
   OpenSSL's read buffer to read the EOF: probes [N], [T] and tls_asio_test's polite/abrupt cases.
4. Bounds. No peer length reaches TlsStream: OpenSSL validates record lengths ([O]); the 512 B
   threshold and the 4 KiB and 64 KiB buffers are the caller's sizes; `take` allocates exactly the
   received count, at most the caller's buffer; the join copies at most 8 KiB of the caller's own
   buffers.
5. Denial of service. Per received segment the head adds one `malloc`/`memcpy`/`free` of the
   segment's size beside the recv and the SSL call both versions make; per record, OpenSSL's read
   buffer is reacquired. Neither amplifies what a peer sends. A slowloris holding a partial record
   pins one read buffer (about 17 KB) per connection, still less than the base's fixed 96 KB.
   Peak per connection while a body is read (64 KiB scratch, up to 64 KiB copy, OpenSSL buffers)
   is bounded and comparable to the base's fixed buffers plus its 64 KiB frame array; header growth
   stays under the parser's limits as before.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | nit | With a caller context that allows renegotiation, a write's `SSL_write` that must read (TLS 1.2 renegotiation) reads the socket directly while the watch's speculative receive may already hold earlier bytes awaiting completion: ciphertext out of order, a fatal record error on that connection only. Unreachable with `reactor::tls::configure` (sets `SSL_OP_NO_RENEGOTIATION`, reactor_tls.h:68) and OpenSSL 3+ refuses client-initiated renegotiation by default; asio::ssl::stream had its own concurrency limits here. | http_asio_tls.h:191, :248-256; reactor_tls.h:68 | Options: keep; or `SSL_set_options(ssl_, SSL_OP_NO_RENEGOTIATION)` in TlsStream's constructor so the invariant holds for any context; or one line in the class comment |
| S2 | no | nit | A peer can make the server reacquire OpenSSL's record buffer once per record (tiny records) and copy once per segment: CPU per byte, bounded, below the AEAD and syscall it already costs; not measured. | code read, http_asio_tls.h:73-74, :221 | none needed; http-tls-small-reply-cost may attribute it with the c1 CPU |

## Not run

- No fuzzing of TlsStream with a TLS-aware fuzzer; the probe's malformed inputs are the two in [O].
- Linux epoll (speculative receive and EOF there) and CI sanitizers: CI's; the branch has no CI run
  at 2d53cef yet.
- TLS 1.2-only clients: the probe negotiates TLS 1.3 (configure allows 1.2; renegotiation is off).
- Slowloris memory and CPU were reasoned from code, not measured.
