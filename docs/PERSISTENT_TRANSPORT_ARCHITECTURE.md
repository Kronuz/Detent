# Validated Architectural Blueprint: bounded persistent delivery transport

**Date:** 2026-10-16
**Status:** Design GO. Implementation and component qualification in progress.

Reuse a fixed number of HTTP/TLS connections per owner executor across logical delivery sessions. The [failed 10k admission run](../benchmarks/fleet-bootstrap-qualification.json) reaches complete readiness but fails during polling. Its agent logs contain `EADDRNOTAVAIL`, and the escaping relay-health sampler error is `ConnectionRefused`. The current client opens, handshakes and closes a socket for every request. Connection churn is established by source inspection; the exact listener-failure cause remains unattributed.

## Ownership and admission

Append an optional opaque cache handle to `ClientOptions`. Absence preserves one-shot behavior. Each owner executor has a fixed-capacity cache shared across source keys, independent of the number of logical sessions. Lease an idle matching entry, otherwise recycle an idle entry or occupy a vacant slot. If all entries are leased, report explicit pressure. Add no waiter list or internal retry.

Match endpoint, hostname, exact TLS-context ownership, credential digest and configured authority/scope. Changing bases, session IDs, windows and consumption modes do not change the connection key. `SessionHandle` and a fresh response decoder continue binding every logical operation. Keep cache entries outside session handles.

Managed coordinators use one cached entry. Fleet owners reserve enough entries for their normal and Close exchange lanes, with a separate reference entry. Bound the total across sources. Idle sockets count toward FD/server-connection and TLS memory admission. Admit cache metadata through the existing storage ledger; account for TLS/socket allocations through the separate physical memory reserve.

Destroy every cache and socket before destroying its executor's execution context. A shared cache handle retains connection ownership, but does not extend an `io_context` lifetime. Qualification explicitly clears its handles before the local context leaves scope.

## Reuse and failure

The response decoder exposes reuse eligibility only after successful complete framing and `http_should_keep_alive()`. Reuse also requires an empty unconsumed input span, no transport error/closure, and no cancellation or deadline expiry. Compute framing eligibility before decoder disposal; release the lease after disposal and original network work settle.

Incomplete responses, framing/schema failures, timeout, cancellation and trailing bytes retire the entry. Do not replay an Open, Next or Close inside the transport. Existing session invalidation, feedback and coordinator retries remain authoritative. A complete valid response carrying `Connection: close` succeeds while retiring its connection; cleanup must preserve that result. Trust failures stay terminal.

Explicitly request keep-alive in shared `OwnedReplyPump` control responses and authority/relay chunked response starts. The pinned HTTP server clamps reuse to the request's permission and processes sequential requests only after final original-write settlement. A client header change alone cannot provide persistence.

## Consolidated component gate

1. Multiple logical sessions reuse one accepted TLS connection with correct control responses, windows and raw/zstd payloads.
2. A held final server write buffers the next request safely without premature settlement or session retirement.
3. Partial bodies, extra bytes, malformed framing and cancellation prevent reuse and drain exactly.
4. Server-requested closure, idle closure and restart retire connections without hidden replay.
5. Endpoint, TLS context, token and scope changes cannot reuse incompatible entries; authorization still runs per request.
6. Cold/warm bootstrap, checkpoint, partial launch and shutdown conserve cache entries and admitted resources.
7. Native, sanitizer and platform checks pass before the matched 10k run. Record connects, handshakes, reuse, retirements, pressure and peak live entries separately from in-flight exchanges.

Preserve the failed run. Capture port-range and TIME_WAIT observations during the matched rerun before attributing the original failure to exhaustion. A one-shot profile remains a connection-churn workload, with its host limits stated separately from persistent-session capacity.

## Admission and cleanup findings

The first persistent 10k run failed during checkpoint with a missing response header. The HTTP framework's offload gate can emit plaintext 503 without `Content-Type`, independently of the relay's typed admission responses. Set each relay reactor's offload limit to the global connection limit: sequential HTTP handling holds at most one offload permit per connection. This permits more queued handlers than the old limit of 32, but leaves worker counts and application lane/body/heap limits unchanged.

The diagnostic rerun completed all 10,000 native checkpoint checks, then failed server-session conservation with 58 retained sessions. It made 17,425 TCP connections and 17,180 retries, concentrated around bootstrap and checkpoint. A successful native result does not establish successful fleet cleanup. The driver discards a handle after a retryable Close failure, and the client previously marked Close settled before receiving its response. Keep the session unusable for Next after Close begins, but mark Close complete only after authenticated 204/410. Explicit caller retries of the same idempotent Close remain bounded to five seconds; transport operations never replay requests internally. Exhausted cleanup fails qualification.

## Current qualification

The component has consolidated source GO with no remaining P1/P2 findings. Native focused checks pass 10/10, the reduced fleet matrix passes 9/9 including certificate preparation, and short raw/zstd 10k runs pass full checkpoint, SDK and server-session conservation. Final ASan/UBSan checks pass 12/12 (40.45 seconds), Linux 12/12 (16.67 seconds), FreeBSD 12/12 (23.82 seconds) and TSan 11/11 (38.13 seconds). TSan omits the 500k relay HTTP case; the other three final matrices include it. The [qualification record](../benchmarks/fleet-persistent-transport-qualification.json) retains run results and fingerprints.

Reused-connection cancellation/deadline checks verify original drainage and retirement; idle peer closure surfaces one failure without hidden replay before an explicit Close reconnects. Malformed controls, including the framework-style plaintext 503, retire the socket and remain terminal schema failures. Header admission now maps storage pressure to typed 503 controls; the corrected 10k runs record zero source/network errors. Typed refusals still close the connection because body intake has not completed.

The direct deterministic held-final-server-write followed by the next request remains an integration coverage gap, with supporting lower-level settlement and real-process evidence. No corresponding source defect is identified. Sustained capacity and physical fleet qualification remain open; these short runs freeze the application baseline for separate library adoption.
