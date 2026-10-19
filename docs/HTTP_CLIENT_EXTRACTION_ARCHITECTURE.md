# Shared HTTP clients

**Updated:** 2026-10-19. **Status:** Validated Architectural Blueprint, accepted for implementation. Complete-component implementation review and release qualification remain open.

## Scope

Move the blocking poll/publisher client, coroutine delivery transport and HTTPS health transport into optional targets in `Kronuz/http`. Keep one request representation and one bounded incremental response parser beneath blocking and Asio drivers. Buffered responses drain that same parser. Delete the migrated private parser, cache and transport bodies.

Detent retains polling ETags and patch interpretation, endpoints, credentials, source selection, distribution framing/schema decoding and Open/Next/Close semantics. HTTP never replays a logical session operation or creates a cleanup runtime. The [resource foundation](RESOURCE_EXTRACTION_ARCHITECTURE.md) supplies canonical allocation ownership.

## Dependency boundaries

| Target | Dependency closure |
| --- | --- |
| `http::client_core` | `http-parser`, `allocators::resources`, shared wire helpers |
| `http::client` | Core and compiled POSIX blocking driver |
| `http::client_asio` | Core and standalone Asio |
| `http::client_tls` | Asio driver and `reactor::tls`/OpenSSL 3 |

Preserve `http::http` and existing server defaults. Client-only configuration with server, Asio and TLS disabled must avoid router, compressors, reactor, Asio and OpenSSL discovery. The parser's enum-reflection dependencies remain part of the declared closure. Resource clients do not build allocation interception or retirement. Compiled targets respect `BUILD_SHARED_LIBS`.

Keep distribution raw/zstd decoding in Detent. Add generic HTTP content decoding only if an existing migrated behavior requires it; it must bound decoded output independently of encoded bytes and workspace.

The legacy poll/publisher adapter currently accumulates the complete wire response without a bound. Its zstd helper allocates the frame's declared output size and gzip decoding appends without a decoded-size limit. The migration must use the existing bounded zstd helper and establish bounded gzip decoding in `compressors`, preserving one codec implementation. Qualify encoded, decoded and workspace limits independently, including highly compressible hostile responses. Neither a wire limit nor a post-decompression size check closes this finding.

## Framing and bounded work

Wrap the existing `http-parser` once. Begin with the existing 64 KiB header, 128-field and 4 KiB read defaults; give body and total-wire limits finite documented values, with application-derived streaming ceilings. Count raw status/header syntax and chunk overhead, including bytes outside callbacks. Bound informational responses.

Reject malformed syntax, control characters, obsolete folding, conflicting lengths, TE+CL, unsupported transfer codings, upgrades, unsupported CONNECT transitions and unexpected trailing responses. Parse `Connection` as tokens. HEAD/204/304 remain bodyless independently of representation length. Fixed-length and chunked framing can persist; explicitly enabled EOF framing always retires its connection. Delivery keeps its stricter HTTP/1.1, unique-header and no-trailer admission policy.

Legacy tests accept repeated equal Content-Length fields. Extend `http-parser` with a narrow per-parser opt-in while preserving its existing default rejection. Conflicting values remain errors. Do not keep the handwritten parser to preserve this case. Document and qualify any parser-structure ABI change, requiring coordinated C++ rebuilds.

Expose consumed input and a small header/body admission seam. Stop socket reads when a consumer retains input or needs CPU work. Delivery continues budgeted fragment/schema work without accumulating the response. Canonical owning allocation contexts cover managed controls, buffers and surviving weak owners; a forwarding allocator cannot charge twice.

## Deadlines and settlement

Capture one steady-clock expiry before connection acquisition. Address attempts, connect, TLS handshake, writes and response reads consume it; progress never restarts the timeout. Check duration arithmetic.

The POSIX driver uses nonblocking sockets and `poll`, handling EINTR, SIGPIPE and descriptor ownership. Pre-resolved endpoints provide the strict socket deadline path. Hostname convenience may use synchronous OS resolution, explicitly documenting that uncancellable DNS and original-operation cleanup can exceed return latency. Add no hidden resolver thread.

Asio operations retain the context, stream, request bytes, read buffer, parser and consumer through authentic original completion. Cancellation and expiry request cancellation; they do not fabricate settlement. Join the losing operation before releasing owners. Finish budgeted disposal with cleanup cancellation suppressed, preserving the original terminal result.

## Cache and TLS

Move the existing bounded cache behind one owner executor, with fixed capacity, no waiter list and explicit pressure when all entries are leased. Match endpoint, hostname, immutable TLS-context ownership and trust identity. An opaque reuse binding carries Detent's credential/authority/scope digest; session IDs, windows and bases stay outside the transport key.

Reuse requires complete accepted framing, no unread bytes, no transport/cancellation/deadline error and consumer acceptance. Semantic failure poisons the entry. A valid Connection-close response succeeds while retiring it. Idle entries retain their FD/TLS admission, and caches retire before their executor disappears. Default shared replay is none; the legacy adapter's single GET reconnect retry consumes the same overall deadline.

Keep TLS configuration and verified-peer helpers in `reactor::tls`. Reuse `configure()` and `bind_hostname()`; prepare contexts on configuration workers. Add authenticated certificate/SPKI metadata and pin checks there only where needed. Pins supplement chain/name verification; empty pins preserve existing behavior. Preserve hostname/CN defaults and opt-ins. Trust changes cannot reuse an entry authenticated under an old policy. Consensus voter mapping remains outside HTTP.

## Errors and compatibility

Return well-formed HTTP statuses normally. Distinguish invalid options, framing/limits, pressure, DNS/connect/transport failure, deadline, cancellation and trust failure with a small error contract retaining underlying codes. Detent maps these to existing unavailable/trust/session outcomes. Malformed plaintext framework 503 responses remain terminal for delivery schema validation.

Keep blocking helper signatures as thin semantic adapters, including ETags, patch interpretation, connection aging and sticky failover. Preserve session/probe/reference behavior and health semantics. Update library API docs, examples and the catalog with the implementation series.

## Qualification

1. Exercise parser fragmentation, zero/tiny budgets, fixed/chunked/EOF bodies, informational/bodyless statuses, duplicate/overflowing headers, trailers, excess bytes and allocation/constructor/weak-owner failure paths.
2. Cover held connect/handshake/write/read originals, deadlines, cancellation, peer closure, saturated caches, executor mismatch, changed trust and exact refunds.
3. Close the [held-final-write gap](PERSISTENT_TRANSPORT_ARCHITECTURE.md): park the authentic server completion after final bytes reach the client, send the next request on the same connection and prove its handler remains blocked while original response admission stays charged. Release that completion and require one settlement, correct next window/feedback and conservation. Cover exact-length and chunked endings, cancellation, shutdown and stale completions. Sleeps and fabricated settlement do not qualify this gate.
4. Run native macOS/Linux/FreeBSD and relevant ASan/UBSan/TSan, HTTP server regressions, Detent integration, Xapiand compatibility and clean minimal configure/link profiles. Qualify coherent public pins without local overrides.
5. Record five alternating short pairs plus representative blocking keep-alive, cold/warm TLS, small control, large raw/zstd and multi-session workloads. Measure CPU/request and CPU/GiB, maximum owner turn, latency, allocation/high-water bytes, connection/reuse/FD counts and executable/dependency growth. Investigate changes above the existing 5% CPU/throughput/memory and 10% latency/turn gates. Relocation establishes no efficiency improvement.

Complete this whole component before one consolidated review. Acceptance requires all three consumers sharing the implementation, no parallel mechanisms, authentic held-write coverage, verified optional closures, clean immutable dependency builds and no unresolved P1/P2 findings. Sustained fleet capacity remains a separate release gate.
