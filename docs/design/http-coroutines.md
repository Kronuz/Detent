> Status: DRAFT (2026-11-02) Design for workstream W2 of [PLAN.md](../PLAN.md) milestone M1 "on the tip"; ready for preliminary review. Ledger: [status/http-coroutines.md](../status/http-coroutines.md).

# HTTP coroutines in Detent

Detent's three HTTP handlers run as coroutines on the reactor loops, with no worker pool.

- Workstream: W2 HTTP coroutines (designer). Branch `design/http-coroutines`, from `Detent:aa00b0e`.
- Library heads read: http `679c0f8`, reactor `ade618d`, http-log `431b878`, cluster `428d57d`
  (each `origin/main` on 2026-11-02), and `cpp-libraries/docs/UPGRADING-2026-10.md` at
  `cpp-libraries:5eedcc7`.

## Abstract

At its head, http runs `HttpHandler::handle` as an `asio::awaitable<void>` on the connection's
loop. Its worker pool and `should_offload` are gone, and nothing runs off a loop. Detent's three
handlers are the classic distributor (`detentd::Distributor`), the durable authority
(`detent::authority::Service`) and the relay (`detent::relay::Service`). Today they block a
worker thread while consensus decides, park a reply in `http::DeferredReply`, and pass admission
from the body hook to the handler through `Request::user_data`. This design moves each handler
onto its loop:

1. A small one-shot `Outcome<T>` carries a result from another thread back to the waiting
   coroutine.
2. Admission moves to `create_extension`, typed as a `RequestExtension`.
3. Body caps keep every accepted upload size unchanged.
4. Detent's JSON refusals stay the only refusals clients see.

Every CPU estimate below is unmeasured and labelled as an estimate. The lead's A/B (T9) decides
performance.

Two premises in the W2 brief do not hold at http `679c0f8`. `Request::user_data`
(`http_message.h:126`) and `ResponseWriter::defer()` (`http_handler.h:105`, `http_deferred.h:251`)
still exist. Detent stops using both anyway (PLAN done criterion 3), and Q4 asks whether http
removes them.

## What Detent runs on the removed path today

| Site | Today | Why it cannot stay |
|---|---|---|
| `src/detentd/detentd.cc:125` `should_offload` | PUT/PATCH on workers, GET inline | Gone at head |
| `src/detentd/detentd.cc:310-347` `submit_command` | Blocks the worker on `pending_cv_.wait_for` for up to 10 s (`:306`, `:333`) | A blocking wait on a loop |
| `src/detentd/detentd.cc:146-171` `CappedBodySink` | 16 MiB cap; over it, the connection drops | Duplicates `set_max_body` |
| `src/detentd/main.cc:278,284` | `HttpAsioService(handler, reactors, workers, 1024, ...)` | Constructor changed |
| `src/detent_authority/service.hh:56` `should_offload` | Everything except health on workers | Gone |
| `src/detent_authority/service.cc:675-700` | `admit()` into `user_data`, then a capped sink | Replaced by `create_extension` |
| `src/detent_authority/service.cc:964-1013` | `response.defer()`; the runtime thread completes it | Detent stops using `defer()` |
| `src/detent_authority/service.cc:1016-1030` | Recording-writer fallback: `future.wait_until` | A blocking wait |
| `include/detent_relay/service.hh:41`, `src/detent_relay/service.cc:352-421` | Offload; `user_data` carries the lease | As above |
| `src/detent_relay/host.cc:243` | `HttpAsioService(*facade, 2, 2, connections, ...)` | Constructor changed |

Detent's own code builds no `reactor::TcpServer` and calls no `Reactor::pool()`. It uses
`reactor::tls`, `ConnectionAdmission` and `UdpOptions`, and all three exist at reactor `ade618d`.
The reactor change reaches Detent only through `HttpAsioService` and through the cluster and
consensus pins (W3). The HTTP clients (`src/detent_delivery/response.cc`,
`include/detent_transport/https_client.hh`, `detent_http`) use http-parser alone. Its stricter
parsing is W3's input-class diff, not part of this design.

## Decisions

**D1. Every handler is a coroutine; synchronous work stays synchronous.** `handle` and the
classic `Router` routes return `asio::awaitable<void>`. The routes for GET snapshot, health and
relay or authority intake never suspend. No callback that outlives `handle` captures the
`Request` or the `ResponseWriter`.

**D2. One mechanism for completion from another thread: `detent_http::Outcome<T>`.** It is a
header-only one-shot (`include/detent_http/outcome.hh`, about 100 lines of Asio):

1. `settle(T)` may run on any thread. Only the first call counts.
2. `wait_until(deadline)` runs on the loop and returns the value or `nullopt`. It is
   `asio::async_initiate` with a cancellation slot, raced against a `steady_timer`.
3. `abandon()` runs on the loop and wakes the waiter with no value.

The invariant is that `settle` posts to the loop's executor only while a waiter is stored, and
the waiting coroutine cannot finish before that post runs. A settle that arrives after the
handler returned stores or drops the value, and it never touches a loop that may already be
destroyed.

`Outcome<T>` replaces `pending_cv_` (classic Raft) and the authority's `defer()` and
`future.wait_until`. Two alternatives were not selected:

- **`asio::experimental::concurrent_channel`:** its executor can be released on the runtime
  thread after the loop's `io_context` is gone.
- **The `task` library's `as_awaitable`:** Detent does not pin `task` (`cmake/dependencies.json`),
  and one adaptor does not justify a new dependency.

**D3. Admission happens in `create_extension` and lives in a typed `RequestExtension`.** At head,
the headers-complete hook enters http's gate (`http_asio.h:752`), then calls
`create_extension` (`:755`), and only then chooses the body intake. Each service returns its own
extension, and `handle` reads `request.ext<T>()`:

| Service | Extension holds | Refusal (unchanged bytes) |
|---|---|---|
| Authority | `shared_ptr<Admission>` (`service.cc:589` `admit`), later the reply reservation (D9) | 401/403/503 JSON `RequestRejected` |
| Relay | `shared_ptr<Impl::Admission>` with its lane lease | 403/404/503 JSON |
| Classic | A write slot for PUT/PATCH, at most 1024 per process (today's `queue_limit`) | 503 `Service Unavailable` + `Retry-After: 1`, before any body byte |

The handler-side fallbacks `request.user_data ? ... : admit(request)` (authority `service.cc:955`,
relay `service.cc:416-421`) are removed, so a request has one admission path. The extension is
destroyed with the `Request`, after the response is written (`http_asio.h`, buffered path).
Anything it holds is therefore held through the write.

**D4. http's gate never refuses a Detent request.** The delivery client accepts control replies
only as `application/json` with status 400, 401, 403, 409, 410, 429 or 503
(`src/detent_delivery/response.cc:177,186`). Any other 503 becomes a protocol error rather than
`AdmissionFull` (`client.cc:435`), and http's gate answers in `text/plain`. Each service sets
`in_flight_limit` as follows:

- **Authority:** 1024, its connection cap.
- **Relay:** `options.connections`, its connection cap.
- **Classic `detentd`:** 0 (no cap). Its GETs are not admitted today either.

A connection holds at most one request between headers-complete and the settled response, so
a per-reactor limit equal to the connection cap cannot bind. T4 proves it. Detent's own
admission (D3) stays the cap. Q3 offers the alternative.

**D5. Body caps: http's cap where bytes land in `Request::body`, Detent's sink where they land
in an accounted lease.**

| Route | Accepted today | After | Mechanism after |
|---|---|---|---|
| `detentd` PUT/PATCH `/v1/snapshot` | ≤ 16 MiB (`detentd.cc:146`); over it, the connection drops | ≤ 16 MiB; over it, 413 | `set_max_body(detent::kDefaultMaxBytes)` (`json_schema.hh:20`) |
| Authority `/v2/open`, `/v2/close`, `/v2/transactions`, `/v2/read`, `/v2/result`, `/v2/distribution` | ≤ `request_bytes` (default 256 KiB, at most 16 MiB by `state.cc:313`); over it, the connection drops | Same bound; over it, 413 | `set_max_body(request_bytes)`; `Sink` (`service.cc:180`) removed |
| Authority session open/next/close | ≤ lane `body_limit`, 413 JSON `body_bound` | Unchanged | `SubscriptionSink` into the lease, read from the extension |
| Relay session routes | ≤ lane `body_limit`, 413 JSON | Unchanged | Relay `Sink` into the lease (`service.cc:376`); `set_max_body` = largest lane limit |
| Health (authority, relay) | Body refused | Unchanged | Header check in `create_extension`, as relay does at `service.cc:354-356`; the zero-capacity sinks go |

The boundaries match. The old sink accepts exactly the cap (`detentd.cc:152`), and http refuses
only `Content-Length > cap` (`http_request_parser.h:255`). No Detent route accepts more than
16 MiB today. 5M-knob data (a 261,337,255-byte logical snapshot, `CAPACITY_QUALIFICATION.md:127`)
travels in responses through owned streams, which `set_max_body` does not cap. `BodyStream` is
unused: no route needs a body larger than these caps or needs to handle one while it arrives.

**D6. CPU-heavy work: inventory and placement.** The following moves off the HTTP workers.
Estimates assume one core at typical rates (rapidjson DOM about 150-300 MB/s, zlib level 6 about
20-60 MB/s, zstd level 3 about 300-500 MB/s). None are measured.

| Work | Where today | Size (estimate) | Placement after |
|---|---|---|---|
| Classic commit (parse, diff, serialize, gzip and zstd twice, persist, ETag; `detentd.cc:178-240`), held under the exclusive `snapshot_mutex_` | HTTP worker (unclustered); cluster Bus thread (clustered, `on_raft_apply`) | 15-40 ms at 560 KB; 0.4-1.2 s at 16 MiB | **Q1**: recommended one Distributor commit thread; precompute outside the lock, swap under it in microseconds |
| Classic composed-diff GET (`detentd.cc:576`) | Already on the loop | Bounded by 64 history entries | Unchanged |
| Authority request parse and validation (`service.cc:706`, `:849`, `:880`) | HTTP worker | ~1-3 ms at 256 KiB; ~50-150 ms at a 16 MiB configuration | Inline on the loop |
| Authority consensus, storage, snapshot and distribution encoding | Authority runtime thread (`service.cc:568`) | n/a | Unchanged |
| Relay frame encoding | Relay owner thread (`host.cc:358`) | n/a | Unchanged |

Distribution encoding never ran on HTTP workers. The rename of
`parse_session_request_on_worker` to `parse_session_request` is part of step S4.

**D7. Owned streaming stays.** Distribution, subscription and relay replies keep
`response.stream()` and `OwnedReplyPump`. At head the buffered `write()` appends until `handle`
returns (`http_asio.h:209`), so owned streaming is the only streaming-reply mechanism, and it
already handles shutdown and settlement.

**D8. Authority deadlines.** The outcome deadline stays `handle` start plus `timeout`, and the
runtime still answers 503 `outcome_unknown` at that deadline. The handler waits until
deadline + 1 s as a backstop. A buffered reply's write is bounded by the request deadline that
http computes at the top of the keep-alive loop, which replaces the deferred reply's own
deadline + 1 s (`AUTHORITY_TRANSPORT.md:55`). `main.cc:279` therefore sets:

- `set_request_timeout(2 × timeout + 1 s)`;
- `set_header_timeout(timeout)`;
- `set_inactivity_timeout(timeout)`.

Headers and each body read keep their current bound. A trickled body can take up to
2 × timeout + 1 s in total.

**D9. Reply budget.** The authority's `complete` callback settles `Outcome<Reply>`. On the loop,
`handle` runs today's budget check (`service.cc:975-1000`) and moves the `ReplyAdmission` into
the extension, so `reply_bytes` stays reserved until the write ends. Today these cases abandon
the deferred reply and close the connection without answering. They now answer:

- Over budget: 503 `{"error":"admission_full"}` and close.
- Over size: 413 `{"error":"reply_bound"}`, which the runtime already emits (`service.cc:2343`).

**D10. Cancellation, shutdown and lifetime.** Only handlers that wait register
`Request::on_cancel(... outcome->abandon())`. GET and health never ask, so they pay nothing for
the disconnect watch (http CHANGELOG 2026-11-01). The two cancel reasons answer differently:

- **Shutdown:** the reply is 503 `{"error":"stopping"}`.
- **Client disconnect:** the handler returns, and its reply is attempted.

The command still commits, as it does today. A classic waiter erases its pending slot on
timeout or cancel. Destruction order already fits: in `main.cc`, `service` is destroyed before
`app`; in `host.cc`, `listener` is destroyed before `facade`. `HttpAsioService::stop()` waits for
every handler frame (coroutine-handlers.md, "Shutdown"), and an `Outcome` settled after that
drops its value on the settling thread.

**D11. `--workers` is removed** from `detentd`: the flag, `DETENTD_WORKERS` and the config field
(`main.cc:124,177`). This is Q2.

**D12. Tests drive handlers through one helper.** `detent_test::run_handler(handler, request,
writer)` runs `create_extension`, the body intake and `handle` to completion on a private
`io_context`. It replaces the synchronous `handle()` calls in about 30 test and benchmark sites,
and `DeferredWriter` (`tests/detent_authority/test_service.cc:21`). The deferred-reply TLS test in
`tests/detent_authority/test_transport_https.cc:14-86` tests http's own `defer()`. It becomes a
pipelined coroutine reply over TLS.

## User-visible changes

| Change | Who sees it |
|---|---|
| Oversize PUT/PATCH and authority bodies get 413 `Payload Too Large` (text/plain, close) instead of a dropped connection; a `Content-Length` over the cap is refused before any body byte | Publishers sending > 16 MiB or > `request_bytes` |
| The old offload 503 (`Service Unavailable`, keep-alive) is gone; classic write-slot refusal is 503 + `Retry-After: 1` before the body | Classic publishers past 1024 pending writes |
| Authority reply over budget or size: JSON 503 or 413 instead of a close (D9) | Publisher readers |
| `CONNECT` is answered by the existing routing (classic 404 or 405; authority 401, or 400 with a valid token; relay 404) and the connection closes; after HTTP/1.0 with `Transfer-Encoding` the connection closes; trailers no longer reach `header()` | Non-conforming clients |
| http-parser's strict mode drops requests with a bare LF, obs-fold, or a non-`chunked` `Transfer-Encoding`, without a reply | Non-conforming clients; Detent's own clients are checked in T7 |
| `stop()` cancels waiting requests with 503 `stopping` and forces stalled sockets after 5 s | Clients during shutdown |
| `--workers`, `DETENTD_WORKERS` and the config field `workers` are gone (Q2) | Operators, `docs/USAGE.md:160`, `website/.../cli/detentd.md`, test harnesses |

## Limits

- A handler that later gains heavy synchronous work stalls its whole loop, and nothing reports
  it. T3's lag probe catches only exercised paths.
- D4 relies on one request per connection. If http ever served concurrent requests on one
  connection, its text/plain 503 could reach the delivery client, and nothing would report it.
  T4 pins the invariant.
- The authority parses up to `request_bytes` on the loop. A 16 MiB configuration stalls a loop
  for an estimated 50-150 ms per maximum request.
- If Q1 chooses inline commits, a 16 MiB classic publish stalls one loop for an estimated
  0.4-1.2 s.

## Designs this depends on or changes

1. **PLAN W2:** this design changes how W2 is read. `in_flight_limit` is a backstop and not the
   cap (D4), and `BodyStream` is not used (D5).
2. **[HTTP_LOGGING_ARCHITECTURE](../HTTP_LOGGING_ARCHITECTURE.md):** the offload 503 and deferred
   replies it covers go away. Authority publisher replies become buffered and visible to
   `AccessLog` when `handle` returns, so only owned streams still need its completion observer.
3. **[ASYNC_AUTHORITY_ARCHITECTURE](../ASYNC_AUTHORITY_ARCHITECTURE.md):150:** its deferred-reply
   choice for the publisher path is replaced by D2.
4. **[AUTHORITY_TRANSPORT](../AUTHORITY_TRANSPORT.md):55:** its deadline mechanism is replaced by
   D8.
5. **[RELAY_ARCHITECTURE](../RELAY_ARCHITECTURE.md):162:** "HTTP intake workers" become the loop.
6. **W3:** its reactor, http-parser, http, http-log, cluster and consensus pins land with this
   work.
7. **W4:** independent of this design.

## Gate rows

Builds and suites run under `quiet light` with at most 4 jobs. Configure with
`-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON`. The two sanitizer builds are separate:
`build-asan` (`-fsanitize=address,undefined`) and `build-tsan` (`-fsanitize=thread`), both with
Homebrew LLVM.

| Row | Target | Procedure |
|---|---|---|
| T1 `Outcome` | Settles before the wait, during the wait from another thread, after the deadline, and after an abandon; a settle after the waiter's `io_context` is destroyed is clean under ASan; 10,000 cross-thread rounds are clean under TSan; removing the waiter-registered check fails a named case | `ctest -R outcome` in native, `build-asan` and `build-tsan` |
| T2 Classic waits do not block | `--reactors 1`, clustered without a quorum: a PUT waiting for consensus, then `GET /v1/health` on the same loop, which answers before the PUT's 503; unclustered: a 16 MiB PUT, then a GET, which answers before the PUT completes (Q1 = commit thread) | `tests/detentd/test_raft_cluster.cc` and `test_distributor_basic.cc` new cases |
| T3 No reactor-thread blocking | (a) `tools/check_loop_blocking.sh` finds no `wait_for`, `wait_until`, `future::get`, `condition_variable` or `sleep_for` in handler translation units; (b) with `enable_lag_probe()`, T2's and T6's fixtures keep `reactor(i).lag().snapshot().max_us` under 250,000 while a 10 s wait is pending | Script in CI; the fixtures assert the lag |
| T4 Admission | Each Detent cap (authority requests, per producer, bytes, subscription lanes, relay lanes, classic write slots) refuses with Detent's bytes before the client sends a body byte; every account returns to zero; a TLS client that does not read keeps `reserved_reply_bytes` until its write ends; no request ever sees http's gate (1025 concurrent clients against the authority) | `test_service.cc` and `relay_network_fixture` cases |
| T5 Body caps | Each D5 row: cap accepted and cap + 1 refused, with `Content-Length` and chunked; the 20 MiB classic PUT gets 413, or a reset if the client's write fails first | `test_distributor_basic.cc:112` rewritten; authority and relay cases |
| T6 Cancel and shutdown | `stop()` with waiting publishers: 503 `stopping` written within the 5 s grace; disconnect while waiting: accounts reach zero, and a later runtime settle is clean under ASan and TSan | Authority and classic fixtures |
| T7 Wire behavior | CONNECT, HTTP/1.0 with `Transfer-Encoding`, bare LF, oversize: the replies in the table above; requests captured from `detent_http`, the delivery client and `https_client` replay through strict http-parser with no rejection | New `test_http_conformance.cc` |
| T8 Suites | Full native suite, Python and Rust bindings, `build-asan`, `build-tsan`, and the real-process fixtures (`tests/detent_system`, `test_raft_cluster`, `relay_network_fixture`, `test_multivoter.py`); hosted CI green on every job; T10's grep is empty | `ctest -j4` per build; `pytest tests/detent_system` |
| T9 Latency A/B (lead) | No regression beyond the A/A spread for `detentd_fanout_bench` GET, `read_response_bench`, classic PUT, authority publish and `detent_read_bench` | `cpp-libraries/tools/ab_compare.py`, old binary from `Detent:aa00b0e` against the branch, with an A/A control, under `quiet strict` holds of at most 10 min |
| T10 Removal | `git grep -E 'should_offload\|DeferredReply\|defer\(\|user_data\|BodyReader\|workers'` over `src`, `include`, `tests` and `benchmarks` finds no HTTP use | CI step |

## Open questions for the owner

**Q1. Where the classic commit runs** (D6).

1. One Distributor commit thread (an `io_context` and a thread, the same pattern as the
   authority runtime and the relay owner). It is not a pool: writes are already serial.
2. Inline on the publishing loop, which stalls it for the estimated time.
3. threadpool's `WorkPool`. That is a pool, it waits on W4, and it still needs serialization.

*Recommendation: option 1, with the off-lock precompute, so no GET waits on a long lock.*

**Q2. `--workers`.**

1. Remove it; the error names the removal.
2. Accept and ignore it with a warning for one release.
3. Repurpose it as `in_flight_limit`.

*Recommendation: option 1. Option 2 is a shim (PLAN rule 2), and option 3 changes its meaning
silently. Update `docs/USAGE.md`, the website CLI page and the harnesses in the same change.*

**Q3. http's refusals and Detent's JSON contract** (D4).

1. Keep the gate unreachable and Detent's JSON admission.
2. An additive http change: an application-supplied refusal body and content type for the gate
   and the body cap, so `in_flight_limit` can be the cap.
3. Teach every Detent client to accept text 503 and 413.

*Recommendation: option 1. Option 2 only if the lead wants http's gate as the real cap.*

**Q4 (for the lead).** After W2 lands, nothing in Detent, prism (`da169a9`) or http-log
(`431b878`) uses `Request::user_data` or `defer()`/`DeferredReply`. Should http remove them under
PLAN rule 2, coordinated with `xapiand-74`? *Recommendation: yes, once Xapiand's use is
checked.*

## First steps

| Step | Content | Blocked on |
|---|---|---|
| S1 | `include/detent_http/outcome.hh` and T1, on the current pins | none |
| S2 | Pins: http-parser, reactor, http, http-log and cluster heads, plus consensus where W1 has landed; `HttpAsioService` constructors; `run_handler` (D12); the relay migration (D3, D5) | W1 for consensus |
| S3 | Classic Distributor: coroutine routes, `Outcome` for Raft, write slots, `set_max_body`, commit placement; T2, T5 | Q1 |
| S4 | Authority: `create_extension`, `Outcome<Reply>`, reply budget, deadlines, cancel; tests off `DeferredWriter`; T4, T6 | Q3 |
| S5 | `--workers` removal and documentation; T7, T10 | Q2 |
| S6 | T3 script, T8 under sanitizers, then the lead's T9 | S2-S5 |

## Review asks

1. Is `Outcome`'s invariant (D2) enough to rule out posting to a destroyed loop?
2. Does D4's unreachable gate hold for TLS and for the streamed path?
3. Are D8's deadlines correct for TLS replies that are written after the outcome deadline?
