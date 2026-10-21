# HTTP logging: validated architectural blueprint

## Abstract

Detent reuses Xapiand's logger and HTTP palettes. Shared severity tint and synchronous classic-distributor metadata logging are implemented; authority and relay access logging need a canonical HTTP completion observer because their replies outlive `handle()`. Extend the existing transport and middleware, preserving response ownership, settlement and resource accounting.

| Review | Status | Date |
| --- | --- | --- |
| Scalability, edge cases, dependency/state, security and scale | Validated for implementation; runtime qualification required | 2026-10-20 |

The implementation baseline is HTTP `6a07f99`, reactor `fe8c5ced` and HTTP-log `2f066ee`. The completed synchronous component also distinguishes successful handler end from a merely started response; that query is not transport completion. Color and synchronous qualification is recorded in [dependency upgrades](DEPENDENCY_UPGRADES.md#xapiand-log-colors-and-http-access-logging-october-20).

## Completion ownership

Add a default-null `HttpHandler::create_response_observer(const Request&)` factory. Its uniquely owned observer receives an event containing optional final status, existing `ReplySettlement` outcome and optional complete entity-body bytes. Reuse the existing outcome vocabulary. Exceptions from factory and notification remain diagnostic and cannot alter response handling.

Invoke the factory at headers-complete before extension/body admission. A request-local owner survives rejection unwinding and remains outside the scoped writer. Successful deferred/stream acquisition transfers ownership into the existing exchange only after tracking and capability construction succeed; failed acquisition leaves the owner available for the error response.

```text
Headers → request-local observation
               ├─ Buffered/rejected → original write → notify
               └─ Deferred/stream → existing exchange → settle → notify
```

The existing exchange settlement guards are the sole completion authority. Move the observer and immutable event out under the exchange lock, then notify outside it. Preserve the application's `on_settled` callback and retention lifetime independently; run it first and release moved application leases normally before diagnostic notification. Neither callback may suppress the other. Never notify from handler return, submission acceptance, `end()` or writer destruction.

## Final metadata and failures

Buffered metadata follows successful head serialization, after conditional/range/compression transforms, so 304/206/416 and compressed lengths reflect the framed response. Stream status follows successful head construction; written body size reuses existing accepted-byte accounting. Incomplete outcomes omit body size. A response without a constructed head has no status; a selected 200 with a failed write is not success.

Notify after authentic original writes settle, including cancellation and shutdown. Preserve current final-write/disconnect ordering and next-request exclusion. Deferred adoption/serialization failures select `Failed` before shutdown fallback. Early HTTP rejection and offload 503 responses retain observation through their write attempt. Parse/TLS failures before observer creation remain connection diagnostics. Add no timer, cancellation slot, executor or parallel operation counter; reactor shutdown already drains sessions.

## Middleware and consumers

Extend `AccessLog::Options` with opt-in transport completion observation, requiring metadata-only mode. Preserve synchronous defaults for Xapiand. Completion mode passes the real writer through, forwards `on_error` and all admission/offload hooks, and emits no response at handler return. Compose any inner observer in one owned node, containing callbacks independently.

Logging retains only bounded escaped method/path labels, protocol version and primitive configuration. Keep the existing 32-byte method and 256-byte path bounds; retain no request/writer/body/header views, credentials, query values, exception descriptions or application leases. Disabled observation allocates and formats nothing and posts no work. Error logging still requires observation when success logs are filtered. Notification is once; actual sink emission remains subject to logger filtering, allocation failure and backpressure. Reuse Detent's existing 4,096-entry logger limit rather than adding a queue.

Enable the same completion preset for classic Distributor, authority and relay. Keep authority connection observation on the actual service and preserve `OwnedReplyPump` written/settled callbacks exactly. Drain listeners before decorator destruction and logger shutdown.

## Qualification and publication

1. Extend authentic held-original-write fixtures across buffered, deferred and fixed/chunked responses. Before release require no notification or early lease retirement; afterward require one matching notification and unchanged application settlement.
2. Cover abandonment, timeout, disconnect, cancellation, service shutdown, duplicate settlement, failed acquisition, serialization failure, admission rejection, 503, transformed responses and empty bodies. Include throwing/reentrant observers and factory/logger failures.
3. Preserve real TLS Open/Next/feedback/terminal bytes, session decisions and account conservation. Rebuild Xapiand's existing detailed synchronous consumer separately.
4. Require native/sanitizer suites on macOS, Linux and FreeBSD and clean public-pin consumer builds. Measure alternating quiet pairs for observation absent, filtered and enabled, including slow sinks, short requests and long streams; record CPU/request, allocations, peak memory and structure/frame growth.
5. Publish additive HTTP first, HTTP-log pinned to it next, then catalog and Detent pins. Keep unrelated logger/scheduler, client/resource extraction and btree changes outside this component. Default-null virtual methods preserve source compatibility; rebuild the header/vtable closure without claiming mixed-revision ABI compatibility.

Review the complete component after these gates, rather than each commit. Completion observation is not qualified by the earlier synchronous logs or short color checks.
