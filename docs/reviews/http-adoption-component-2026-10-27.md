# Detent review: shared HTTP client adoption

Date: 2026-10-27. Whole-component correctness and security review, bounded to one pass.

Reviewed: allocators:d1de304..4b39819, http-parser:19ee383..154fad2,
compressors:b28f20e..86ff17d, http:5ac5344..be9efc1 and
Detent:538bf50..dab891d. Checked the actual diffs, shared client sources,
consumer adapters and relevant fixtures against
[the blueprint](../HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md),
[shared decisions](../SHARED_LIBRARY_DECISIONS.md) and
[review calibration](calibration.md).

## Verdict

**NO-GO: two P2 framing findings block source acceptance.** Both are inherited
edges that the extraction's explicit framing contract requires the shared
component to close. Neither finding requires another transport or codec.

## Attackers considered

An untrusted HTTP peer controls response syntax, lengths, chunk extensions,
compression, fragmentation and surplus bytes. A verified TLS peer can still
return hostile application data. Local callers can change credentials or trust
contexts, exhaust admission, cancel operations and retain weak owners.

## Findings and required changes

1. **HTTP-CR1, P2: queued response bytes survive connection reuse.**
   `http/http_client_asio.h:70` reads at most 4,096 bytes and exits when the first
   response completes (`:71`, `:87`). The POSIX driver does the same at
   `http/http_client_posix.cc:197`. Parser excess-byte rejection only examines
   the span supplied to `feed()` (`http/http_client_core.h:183`).
   `http/http_client_tls.cc:94` accepts reuse from the caller's Boolean and
   connected state, and acquisition at `:62` performs no pending-input check.
   Detent's streaming path likewise stops at decoder completion
   (`src/detent_delivery/client.cc:175`) and accepts reuse at `:252`.

   A peer can queue a complete response whose total wire length is exactly
   4,096 bytes followed by another complete response. The first read contains
   no excess bytes, so it succeeds and the connection remains reusable. The
   next exchange writes its request and can consume the queued response as its
   answer. TLS adds decrypted and encrypted buffering beyond the caller's
   input span. This is a direct control-flow finding; a network reproduction
   was not run. The private delivery cache and blocking keep-alive reader at
   Detent:538bf50 have the same omission; it is not attributed to the move.

   **Required:** make transport reuse eligibility account for already buffered
   input, including TLS/Asio buffers, and retire an idle connection with
   unexpected pending input before another request uses it. Keep these checks
   in the shared transport. Add deterministic fixed-length and chunked cases
   where the first message ends exactly at a read boundary and the second is
   already queued, plus an idle-arrival case. Require retirement or rejection
   and prove the second request cannot accept that queued message. This does
   not claim a readiness check can predict future peer traffic.

2. **HTTP-CR2, P2: malformed chunk extensions bypass syntax admission.**
   `http/http_client_core.h:174` runs its strict raw scan only until the final
   header terminator. The underlying parser's
   `http-parser/http_parser.cc:1908` then ignores every chunk-extension byte
   except CR, including bare LF, NUL and DEL. For example, after valid chunked
   headers, `1;foo=bar\nignored\r\na\r\n0\r\n\r\n` reaches normal
   message completion by that state transition. Strict mode checks only the
   LF following CR at `:1922`; it does not validate the extension. This
   contradicts the promised malformed-syntax/control rejection and permits
   interpretation differences across HTTP parsers. The state is also present
   at http-parser:19ee383; sharing it does not close this requirement.

   **Required:** validate chunk-extension syntax in the existing parser,
   using an explicit strict client policy if compatibility requires it.
   Reject forbidden controls and malformed extension syntax while preserving
   valid token and quoted values. Exercise every split position, including
   one-byte feeds, and require failure plus non-reusability for those vectors.
   Do not add a second chunk parser in Detent.

## Checks that hold in the inspected source

1. Allocation ownership uses the canonical context and unique-owner helper;
   construction failure refunds the original allocation. The server preset
   captures its PMR origin. `ResponseParser` cannot copy or move its internal
   self pointer. Owning request/response members retain their resource through
   buffer destruction.
2. Repeated numeric-equal lengths are opt-in; conflicting lengths and TE+CL
   remain errors. Header admission precedes body delivery, and wire accounting
   includes framing overhead. Delivery passes its computed 64-bit wire ceiling
   into both shared limits (`src/detent_delivery/response.cc:33`, `:91`), without
   the obsolete 1 GiB cap. Its 2 GiB configuration test uses a small payload;
   it does not prove actual transfer above 1 GiB.
3. The deadline race joins the losing original operation. Delivery suppresses
   cancellation throws during budgeted disposal and preserves terminal failure
   (`src/detent_delivery/client.cc:212`). Cache keys retain exact context
   ownership, hostname, endpoint and the credential/authority/scope binding.
   Semantic acceptance controls reuse. The gzip adapter uses the existing
   decoder's cumulative bound, with at most 16 KiB returned per iterator turn.

## Evidence and remaining gates

Inspected terminal logs report HTTP 14/14 in 7.59 seconds and focused Detent
54/54 in 136.10 seconds
([HTTP log](../../../http-tests.log), [Detent log](../../../detent-focused-tests.log)).
These are the lead's runs. No independent build, test or probe ran: `quiet check`
returned exit 1 for other projects' exclusive holds. No source was changed.

After the two fixes, retain these gates:

1. Run the pointed regressions and scoped sanitizer checks. Qualify authentic
   held connect/handshake/write/read completion and cancellation, plus the
   existing held server-final-write fixtures, on the final revisions.
2. Finish terminal upstream CI and coherent public-pin consumer builds without
   source overrides. Rebuild all consumers for the parser structure and
   deflate constructor/layout changes. Qualify minimal/shared profiles,
   Xapiand compatibility and the required macOS/Linux/FreeBSD matrix.
3. Close real large-response, fuzzing and cost qualification at the final
   revisions. This review measures no CPU, memory or latency improvement and
   waives no threshold. EX-1 first-read performance remains a separate NO-GO.

## Re-check, 2026-10-28

Reviewed only the two findings against http:be9efc1..c352ae0,
http-parser:154fad2..87ae8cf and the public candidate pins at Detent:03c0722.

**NO-GO: both findings are partially closed.** The remaining requirements are
within the original findings; the original ruling above is preserved.

1. **HTTP-CR1 remains open: TLS input BIO bytes are unchecked.**
   `http/http_client_tls.cc:10` checks `SSL_pending`, `SSL_has_pending` and
   socket availability. Cache release and acquisition now use that guard, and
   the fixed-length, same-record and idle-arrival fixtures exercise those
   paths. POSIX checks input before writing and closes nonreusable connections.

   Asio 1.36's `ssl/detail/impl/engine.ipp:227` writes received ciphertext into
   a BIO pair, and `ssl/detail/io.hpp:263` can supply multiple records from one
   socket read. `SSL_has_pending` checks OpenSSL's record-layer buffers;
   it does not count bytes waiting in the input BIO. See the supported
   [OpenSSL 3.0 implementation](https://github.com/openssl/openssl/blob/openssl-3.0.0/ssl/ssl_lib.c#L1456)
   and its [record-layer check](https://github.com/openssl/openssl/blob/openssl-3.0.0/ssl/record/rec_layer_s3.c).
   With separate TLS records coalesced into one socket read, the first response
   can complete while the next record remains in the BIO, outside all three
   checks. This is source-level evidence; that variant was not executed.

   **Required:** include input-BIO pending bytes in shared reuse eligibility.
   Add a deterministic regression with separate encrypted
   records received together, requiring no pending socket or record-layer
   bytes while BIO input remains. The current fixture at
   `test/client_tls_test.cc:23` sends the concatenated responses through one
   TLS write. Also supply the originally required chunked boundary case;
   `test/support/response_boundary.h:9` generates only Content-Length framing.
   Prove the subsequent request cannot receive the queued response.

   No separate Asio buffer API is requested. Its pinned receive buffer is
   17 KiB (`stream_core.hpp:34`), matching the
   [default BIO-pair capacity](https://docs.openssl.org/3.0/man3/BIO_s_bio/).
   In the serialized read path with the supplied default TLS modes,
   `want_input_and_retry` follows exhaustion of the input BIO, so `put_input`
   can accept that whole receive block. The missing observable layer is the
   BIO itself; this reasoning does not cover callers replacing BIOs or modes.

2. **HTTP-CR2 remains open: extension grammar is still unchecked.**
   `http-parser/http_parser.cc:1908` now rejects forbidden control bytes,
   closing the demonstrated LF/NUL/DEL bypass. It still accepts every other
   byte until CR without tracking names, values or quoting. For example,
   `1;=bad\r\na\r\n0\r\n\r\n` and
   `1;foo="unterminated\r\na\r\n0\r\n\r\n` reach normal completion.
   The new tests cover control-byte splits, not these malformed productions
   or repeated one-byte feeds. **Required:** finish extension grammar
   validation in the existing parser and exercise invalid empty names,
   unterminated quotes and escapes, while preserving valid token/quoted
   values. Keep failure nonreusable through the client wrapper.

The `exchanged_` distinction at `http/http_client_tls.cc:116` and `:168`
preserves explicit connect-before-first-exchange with legitimate TLS control
traffic. It adds no request replay. Cache eviction and release still decrement
live/leased counters once, and lease ownership still spans the original await.

Independent validation: `quiet check` returned 2 for light holds;
`quiet light http-review-recheck 3 ... -- ctest --test-dir http-fixes-build
-R '^client_tls$' --output-on-failure -j1` passed **1/1 in 0.09 seconds**.
Inspected lead logs also show the original queued regression failure,
parser **1/1**, the earlier fixed HTTP matrix **14/14**, and latest client
checks **4/4**. No compilation, new probe, sanitizer, performance run or full
matrix was performed in this re-check.

After these scoped corrections, terminal hosted/composed CI, final public
profiles, platform and sanitizer/fuzz qualification, large-response evidence
and costs remain required. No threshold or EX-1 performance gate is waived.

## Closure re-check, 2026-10-28

Reviewed only HTTP-CR1/CR2 against http:c352ae0..c64fb43 and
http-parser:87ae8cf..d2eb839, including their regression fixtures.

**Source GO, conditional on the remaining qualification gates below.** Both
concrete source findings are closed; earlier rulings remain as dated evidence.

1. **HTTP-CR1 closed.** `http/http_client_tls.cc:10` includes input-BIO bytes
   in one `buffers_empty()` predicate. Response acceptance, cache release and
   checkout use that predicate with the existing socket check. At
   `test/client_tls_test.cc:17`, separate TLS writes produce separate records;
   the fixture waits for complete ciphertext records on the receiving socket.
   Before release it proves zero SSL pending bytes, zero socket bytes and
   positive BIO pending bytes, then requires zero live and leased cache
   entries. Both fixed-length and chunked responses end at 4,096 bytes.
   The preceding library fails at the cache assertion after those
   preconditions ([before log](../../../bio-regression/before.log)).
   Explicit connection setup, original-operation ownership and counter
   retirement remain intact; no replay or new transport is introduced.
2. **HTTP-CR2 closed.** `http-parser/http_parser.cc:297` validates extension
   names, token/quoted values, escapes and permitted separator whitespace.
   It rejects the missing-name and unterminated-quote cases. Existing
   `header_state` stores all incremental grammar state; appended enum values
   fit its seven bits and preserve earlier enum values. Chunk bytes remain
   binary, and the HTTP wrapper latches parser failure as nonreusable.
   The preceding parser fails the malformed-name rejection with the new
   fixture ([before log](../../../bio-regression/grammar-before.log)).

Independent check after successful `quiet check`: the existing `client_tls`
suite passed **1/1 in 0.09 seconds** under `quiet light http-review-closure`,
with no compilation. Inspected lead logs show parser **1/1 in 0.33 seconds**
and complete HTTP **14/14 in 6.86 seconds**
([parser](../../../chunk-grammar-tests.log),
[HTTP](../../../bio-fixed-tests.log)). No new matrix or performance run was
performed by this reviewer.

Remaining conditions are terminal hosted and composed CI on these exact
revisions, coherent public consumer pins and matching rebuilds, required
platform/sanitizer/fuzz qualification, real large-response evidence and cost
gates. Extension fixtures cover every two-slice split; literal repeated
one-byte extension feeds remain required qualification evidence and are not
claimed here. No further broad review is requested for that evidence alone.
EX-1 first-read performance remains a separate NO-GO; no threshold is waived.
