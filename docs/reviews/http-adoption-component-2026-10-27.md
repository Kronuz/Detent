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
