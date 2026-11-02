> Status: DRAFT (2026-11-02) Design for workstream W2 of [PLAN.md](../PLAN.md) milestone M1 "on the tip", revised to fold in the architect's REJECT ([reviews/http-coroutines-design.md](../reviews/http-coroutines-design.md), L1-L5, C1-C6, Q1-Q4); ready for implementation. Ledger: [status/http-coroutines.md](../status/http-coroutines.md).

# HTTP coroutines in Detent

Detent's three HTTP handlers run as coroutines on the reactor loops, with no worker pool and no
blocking wait on a loop or an owner.

- Workstream: W2 HTTP coroutines (designer). Branch `design/http-coroutines`, from `Detent:aa00b0e`.
- Library heads read: http `679c0f8`, reactor `ade618d`, http-log `431b878`, cluster `428d57d`
  (each `origin/main` on 2026-11-02), Asio at Detent's pin `231cb29`, and
  `cpp-libraries/docs/UPGRADING-2026-10.md` at `cpp-libraries:5eedcc7`. Sources quoted from http
  and reactor heads are the exports in `.scratch/arch-review/http-head/` and `reactor-head/`.

## Abstract

At its head, http runs `HttpHandler::handle` as an `asio::awaitable<void>` on the connection's
loop. Its worker pool and `should_offload` are gone. Detent's three handlers are the classic
distributor (`detentd::Distributor`), the durable authority (`detent::authority::Service`) and
the relay (`detent::relay::Service`). Today they block a worker thread while consensus decides,
park a reply in `http::DeferredReply`, and pass admission from the body hook to the handler through
`Request::user_data`. This design moves each handler onto its loop:

1. Work that belongs to an owner (the authority runtime, a new Distributor owner) is reached with
   Asio's own hop: `co_spawn` onto the owner's executor with a bound cancellation slot. A later
   completion reaches the waiting work through a capacity-1 `asio::experimental::channel` on the
   owner's executor. Detent adds no completion type.
2. The classic commit runs as an unsliced coroutine on one Distributor owner `io_context`, and
   publishes immutable state through `atomic_shared_ptr`, so no GET or health request waits on a
   commit.
3. Admission moves to `create_extension`, typed as a `RequestExtension`. Detent's JSON refusals
   stay the only refusals clients see (PLAN W2, `Detent:7348e5b`).
4. A submitted command always gets a truthful reply and at least 1 s to write it, and the receive
   window stays T.

W2 needs no library change. Every CPU estimate below is unmeasured and labelled as an estimate.
The lead's A/B (T9) decides performance.

Two premises in the W2 brief do not hold at http `679c0f8`. `Request::user_data`
(`http_message.h:126`) and `ResponseWriter::defer()` (`http_handler.h:105`, `http_deferred.h:251`)
still exist. Detent stops using both anyway (PLAN done criterion 3); their removal from http is a
follow-up (Q4).

## Review response

The architect's preliminary review ([reviews/http-coroutines-design.md](../reviews/http-coroutines-design.md),
REJECT) is folded in as follows.

| Finding | Closed by |
|---|---|
| L1 (A5) co_spawn hop, no `Outcome<T>` | D2 rewritten: `co_spawn` + bound slot + owner-local channel; cancellation binding, lifetimes (including the authority runtime's `io_context`, which today dies before the HTTP service), cost; T1 rewritten as bridge rows |
| L2 (A1) deadline timeline | D8 rewritten: the publisher reply leaves through an owned stream with its own deadline, at least 1 s after the outcome; `request_timeout` stays T, so the receive window and idle keep-alive do not change; T11 added |
| L3 (A2) outcome contract | D9 and D10 rewritten: a command that may commit gets `outcome_unknown` (authority) or the existing retry reply (classic), never `stopping` or `admission_full`; Disconnect keeps waiting; T6 rewritten |
| L4 (A3) classic lock | D13 added: one owner computes off-lock and publishes immutable state; clustered applies post to it; T2 rewritten |
| L5 (A4) T3 | D14 added (`assert_off_loop`); T3 rewritten with the revert mutant; T2 deterministic through a commit hook |
| C1 (A6) commit time | D13 and S3: the owner logs commit duration; the lead records 560 KB and 16 MiB; slicing thresholds stated |
| C2 (A7) D6 inventory | D6 lists the composed-diff GET with its estimate and the relay parse; S4 records the authority parse time; Q5 asks the lead about the composed diff |
| C3 (A8) step order | First steps reordered as A8; every commit on the branch builds |
| C4 (A9) gate rows | T10 precise patterns; T9 names the managed first read; T4 and T6 assert `reserved_reply_bytes` returns to baseline |
| C5 (A10) accuracy | D10 states `listener.stop()` precedes `facade.reset()`; D4 builds the cap and `in_flight_limit` from one value; the user-visible table lists the health-body change and states that half-close and idle keep-alive do not change |
| C6 sanitizers | T1, T4, T6 run in separate `build-asan` and `build-tsan` (T8) |
| Q1-Q4 | Adopted as the architect answered (see "Owner questions") |

## What Detent runs on the removed path today

| Site | Today | Why it cannot stay |
|---|---|---|
| `src/detentd/detentd.cc:125` `should_offload` | PUT/PATCH on workers, GET inline | Gone at head |
| `src/detentd/detentd.cc:310-347` `submit_command` | Blocks the worker on `pending_cv_.wait_for` for up to 10 s (`:306`, `:333`) | A blocking wait |
| `src/detentd/detentd.cc:181`, `:488`, `:544`, `:625` | Commit holds `snapshot_mutex_` exclusively; GET and health take it shared on the loop | A loop waits on a commit (L4) |
| `src/detentd/detentd.cc:349-365` `on_raft_apply` | Clustered commit on the cluster Bus loop | A reactor loop does the commit |
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
The reactor change reaches Detent through `HttpAsioService` and through the cluster and consensus
pins (W3). The HTTP clients (`src/detent_delivery/response.cc`,
`include/detent_transport/https_client.hh`, `detent_http`) use http-parser alone; its stricter
parsing is W3's input-class diff.

## Decisions

**D1. Every handler is a coroutine; synchronous work stays synchronous.** `handle` and the
classic `Router` routes return `asio::awaitable<void>`. GET snapshot, health, and relay or
authority intake never suspend. No callback that outlives `handle` captures the `Request` or the
`ResponseWriter`.

**D2. Cross-thread work uses Asio's hop and an owner-local channel (L1).** Where a handler starts
work that belongs to an owner, it awaits:

```cpp
auto stop = std::make_shared<StopFlag>();   // atomic flag + asio::cancellation_signal
request.on_cancel([stop](http::CancelReason why) {
	if (why == http::CancelReason::Shutdown) stop->raise();   // sets the flag, then emits terminal
});
auto reply = co_await asio::co_spawn(owner, work(std::move(task), stop),
	asio::bind_cancellation_slot(stop->signal.slot(), asio::use_awaitable));
```

The owner work waits for a later completion on an
`asio::experimental::channel<void(asio::error_code, Reply)>` of capacity 1, constructed on the
owner's executor, raced against a `steady_timer` with `asio::experimental::awaitable_operators`.
Every completion is posted to the owner before it settles the channel, so the channel is touched
by one thread. Both headers exist at Asio `231cb29` (`asio/experimental/channel.hpp`,
`awaitable_operators.hpp`). The two owners:

1. Classic: the Distributor owner (D13). It keeps the pending map of correlation ids to channels;
   `on_raft_apply` posts each applied envelope to it.
2. Authority: the runtime's `io_context`. `complete` already runs once, on the runtime thread,
   guarded by `task->settled` (`service.cc:1333-1365`); its callback sends on the channel.

What the hop provides, at Asio `231cb29`:

1. It resumes on the loop. The finished work posts to the owner, then dispatches to the caller's
   executor through `handler_work` (`asio/impl/co_spawn.hpp:156-160`).
2. An exception in the work rethrows in the handler.
3. Cancellation crosses threads safely. `co_spawn_cancellation_handler` dispatches the emit onto
   the owner executor (`co_spawn.hpp:255-264`). From a loop thread that is a post, so the wait
   never resumes inside `Request::cancel`.
4. The work completes once.

The cancellation binding, stated explicitly:

1. http disables the connection coroutine's cancellation around `handle` (`http_asio.h:953`), so
   the slot is bound by hand and only the `Request::on_cancel` bridge emits on it.
2. Only Shutdown emits (D10). Disconnect keeps waiting.
3. `on_cancel` can run its callback inline, before the hop starts, when the request is already
   cancelled (`http_message.h:144-147`). An emit on an unbound slot is lost, so the callback also
   sets the flag, and the owner work reads the flag and its cancellation state before each wait.
4. Cancellation ends an owner-side wait with the truthful reply (D10), never by dropping a
   command that may commit.

Lifetimes:

1. The loop's `io_context` outlives the spawned work, which tracks it through `handler_work`.
   `TcpServer::stop()` waits for sessions before it stops the loops (`reactor.h:595-597`), and
   every handler awaits its hop, so no hop is outstanding when a loop stops. `run_handler` (D12)
   keeps that order.
2. Each owner's `io_context` runs until the HTTP service has stopped. In `main.cc` the handler
   (`app`, `:241`) is declared before `service` (`:278`), so the service is destroyed first.
3. The authority runtime's `io_context` does not satisfy (2) today. It is a member of the local
   `Runtime` in `Service::run` (`service.cc:1059`, `:1141-1152`). When the runtime fails it sets
   `stopping_` (`:1143-1146`), `io.run()` returns, and `Runtime` is destroyed while the HTTP
   service still accepts requests. A hop to its executor after that is a use after free. The
   `io_context` therefore moves to a `Service` member, declared before `queue_`, held by a work
   guard that `~Service` releases after it sets `stopping_` and before it joins. After a runtime
   failure the owner keeps running, and the work refuses with 503 `stopping` before it enqueues,
   which is truthful (`service.cc:791-793`).
4. Each channel dies before its owner's `io_context`. A late completion holds a `shared_ptr` to
   its channel and sends into it after the waiting work has gone, which is harmless. Tasks still
   in `queue_` die in `~Service` after the join, before the `io_context` member (reverse
   declaration order).

Cost: one coroutine frame and about four cross-thread posts per write request. GETs and health do
not hop. T9 covers it.

Not selected:

- **A Detent one-shot (`Outcome<T>`):** Asio's hop and channel already give single completion,
  cross-thread resumption and cancellation, and the owner direction is to add no Detent type.
- **`asio::experimental::concurrent_channel`:** not needed. Every settlement is posted to the
  owner first, so the channel is single-threaded.
- **`task::event`** (task `8cad661`, `task.hh:687-737`, per the review): it carries no value, has
  no deadline, and needs a task scheduler on each loop; Detent does not pin `task`.

**D3. Admission happens in `create_extension` and lives in a typed `RequestExtension`.** At head,
the headers-complete hook enters http's gate (`http_asio.h:752`), calls `create_extension`
(`:755`), and only then chooses the body intake. Each service returns its own extension, and
`handle` reads `request.ext<T>()`:

| Service | Extension holds | Refusal (unchanged bytes) |
|---|---|---|
| Authority | `shared_ptr<Admission>` (`service.cc:589` `admit`), later the `ReplyAdmission` (D9) | 401/403/503 JSON `RequestRejected` |
| Relay | `shared_ptr<Impl::Admission>` with its lane lease | 403/404/503 JSON |
| Classic | A write slot for PUT/PATCH, at most 1024 per process (today's `queue_limit`) | 503 `Service Unavailable` + `Retry-After: 1`, before any body byte |

The handler-side fallbacks `request.user_data ? ... : admit(request)` (authority `service.cc:955`,
relay `service.cc:416-421`) are removed, so a request has one admission path. The extension is a
member of the `Request`, which is a loop-iteration local destroyed after `respond()`
(`http_asio.h:964-969`); `respond()` includes an owned stream's write (`:793`, `:624`). Anything
the extension holds is therefore held through the write.

**D4. http's gate never refuses a Detent request.** The delivery client accepts control replies
only as `application/json` with status 400, 401, 403, 409, 410, 429 or 503
(`src/detent_delivery/response.cc:173-186`), and http's gate answers in `text/plain`. Each service
builds its connection cap and `in_flight_limit` from one value, so the backstop cannot fall below
the cap:

- **Authority:** one `kPublisherConnections = 1024` in `main.cc` for both (today two literals,
  `main.cc:278`).
- **Relay:** `options.connections` for both (`host.cc:243-245`, already one value).
- **Classic `detentd`:** `in_flight_limit` 0 (no cap). Its GETs are not admitted today either.

A connection holds at most one request between headers-complete and the settled response, and a
gate lease spans exactly that (`http_asio.h:747,752,921,966`), so a per-reactor limit equal to the
connection cap cannot bind. This holds for TLS and owned streams (review, "What holds"). T4 proves
it. Detent's own admission (D3) stays the cap (Q3).

**D5. Body caps: http's cap where bytes land in `Request::body`, Detent's sink where they land
in an accounted lease.**

| Route | Accepted today | After | Mechanism after |
|---|---|---|---|
| `detentd` PUT/PATCH `/v1/snapshot` | ≤ 16 MiB (`detentd.cc:146`); over it, the connection drops | ≤ 16 MiB; over it, 413 | `set_max_body(detent::kDefaultMaxBytes)` (`json_schema.hh:20`) |
| Authority `/v2/open`, `/v2/close`, `/v2/transactions`, `/v2/read`, `/v2/result`, `/v2/distribution` | ≤ `request_bytes` (default 256 KiB, at most 16 MiB by `state.cc:313`); over it, the connection drops | Same bound; over it, 413 | `set_max_body(request_bytes)`; `Sink` (`service.cc:180`) removed |
| Authority session open/next/close | ≤ lane `body_limit`, 413 JSON `body_bound` | Unchanged | `SubscriptionSink` into the lease, read from the extension |
| Relay session routes | ≤ lane `body_limit`, 413 JSON | Unchanged | Relay `Sink` into the lease (`service.cc:376`); `set_max_body` = largest lane limit |
| Health (authority, relay) | Relay: 400 JSON `health_body_forbidden` (`service.cc:354-356`); authority: the connection drops (`Sink` capacity 0, `service.cc:677`) | 400 JSON `health_body_forbidden` for both | Header check in `create_extension`; the zero-capacity sinks go |

The boundaries match. The old sink accepts exactly the cap (`detentd.cc:152`), and http refuses
`Content-Length > cap` (`http_request_parser.h:255`) and a chunked body past the cap (`:268`). No
Detent route accepts more than 16 MiB today. 5M-knob data (a 261,337,255-byte logical snapshot,
`CAPACITY_QUALIFICATION.md:127`) travels in responses through owned streams, which `set_max_body`
does not cap. `BodyStream` is unused: no route needs a body larger than these caps.

**D6. CPU-heavy work: inventory and placement (C2).** Estimates assume one core at typical rates
(rapidjson DOM about 150-300 MB/s, zlib level 6 about 20-60 MB/s, zstd level 3 about
300-500 MB/s). None are measured.

| Work | Where today | Size (estimate) | Placement after |
|---|---|---|---|
| Classic commit (resolve, serialize, persist, diff, gzip and zstd of diff and body, ETag; `detentd.cc:178-240`) | HTTP worker (unclustered); cluster Bus loop (clustered, `on_raft_apply`) | 15-40 ms at 560 KB; 0.4-1.2 s at 16 MiB | Distributor owner, unsliced (D13); measured in S3 (C1) |
| Classic composed-diff GET (`detentd.cc:569-608`): compose, serialize, gzip and zstd, outside the lock | Already on the loop | Up to snapshot size: about 0.3-0.8 s of gzip at 16 MiB, plus 30-55 ms of zstd. Bounded in steps (64 history entries), not bytes. Paid once per base version per publish (composed-diff cache) | Unchanged in W2; inherited, not introduced. Q5 (lead) |
| Authority request parse and validation (`service.cc:706`, `:849`, `:880`) | HTTP worker | ~1-3 ms at 256 KiB; ~50-150 ms at a 16 MiB configuration | Inline on the loop; timed in S4 (C2) |
| Relay session request parse (`relay/service.cc:422`, `parse_session_request_on_worker`) | HTTP worker | Bounded by the lane `body_limit`; ~3-7 ms per MiB | Inline on the loop |
| Authority consensus, storage, snapshot and distribution encoding | Authority runtime thread (`service.cc:568`) | n/a | Unchanged |
| Relay frame encoding | Relay owner thread (`host.cc:358`) | n/a | Unchanged |

Distribution encoding never ran on HTTP workers: the pump publishes buffers the runtime already
encoded (`reply_pump.cc:40-46`). `parse_session_request_on_worker` becomes
`parse_session_request` in S4.

**D7. Owned streaming stays, and carries the publisher reply.** Distribution, subscription and
relay replies keep `response.stream()` and `OwnedReplyPump`. At head the buffered `write()`
appends until `handle` returns (`http_asio.h:209`), so owned streaming is the only mechanism whose
write has its own deadline (`write_owned_response`, `http_asio.h:568-619`, bounded by
`exchange->deadline()`). D8 uses it for the authority's publisher replies.

**D8. Authority deadlines: a per-phase timeline (L2).** At head one `request_deadline` is taken at
the top of the keep-alive loop (`http_asio.h:815-817`). It bounds the idle wait, the header and
body reads (`:825`, `:931`), drops a request whose body ends after it (`:939`), and bounds a
buffered reply's write (`:794`). It does not bound an owned stream's write, which uses the
stream's own deadline (`:624`, `:604-606`). The timeline, with T = the authority `timeout`
(default 5 s, `service.hh:46`):

| Phase | Bound | Set by |
|---|---|---|
| Idle, headers and body (the receive window) | T from the top of the keep-alive loop | `set_request_timeout(T)`, unchanged (`main.cc:279`) |
| Outcome | T from `handle` start; the runtime answers 503 `outcome_unknown` at that deadline (`service.cc:1780-1787`) | `options_.timeout`, unchanged |
| Backstop | The handler stops waiting 1 s after the outcome deadline and answers `outcome_unknown` | D2's timer |
| Reply write | Until `max(now, outcome deadline) + 1 s`, measured when the reply is ready | `StreamOptions::reply.deadline` of an owned stream |

Every reply written after a submission leaves through an owned stream: `start(metadata, length)`,
one `publish(chunk)`, `finish()` (`http_owned_stream.h:301-310`), with `chunk_bytes`,
`maximum_bytes` and `reply.limits.body_bytes` set to `reply_bytes`. A submitted command therefore
always has at least 1 s to write its reply, whatever the receive took, on TLS and on plain TCP.
That is the deferred reply's contract today (`service.cc:966-967`, `AUTHORITY_TRANSPORT.md:55`),
carried by the mechanism the authority already uses for distribution (`service.cc:832-834`).
Replies before a submission (refusals, parse errors, health) stay buffered under the request
deadline, as today.

Nothing about the receive changes: `request_timeout` stays T, so a trickling client holds an
admission slot at most T, and idle keep-alive connections live T. No header, idle or inactivity
timeout is added. The architect's alternative (stamp headers-complete, refuse when the body took
longer than T, and raise `request_timeout`) needs the idle phase in its budget as well, because
the deadline starts before the idle wait; it also lengthens every phase that the deadline bounds.
T11 is the trickled-body row.

**D9. Reply budget (L3).** The runtime's `complete` callback sends the `Reply` on the work's
channel (D2). The owner work runs today's budget check (`service.cc:975-1000`) on the runtime
thread and returns the reply with its `ReplyAdmission`. The handler moves the `ReplyAdmission`
into the extension, so `reply_bytes` stays reserved until the owned stream's write ends (D3), and
`ReplyAdmission`'s destructor releases it (`service.cc:500-511`). Today these cases abandon the
deferred reply and close the connection without answering. They now answer:

| Case | Command (`/v2/open`, `/v2/close`, `/v2/transactions`) | Read (`/v2/read`, `/v2/result`) |
|---|---|---|
| Over the reply budget | 503 `{"error":"outcome_unknown"}`: the command has executed; a same-identity retry returns the retained outcome (`AUTHORITY_ARCHITECTURE.md:81`) | 503 `{"error":"admission_full"}` |
| Over `reply_bytes` | Not reachable for a command reply | 413 `{"error":"reply_bound"}`, which the runtime already emits (`service.cc:2343`) |

**D10. Cancellation, shutdown and lifetime (L3).** Only handlers that hop register
`Request::on_cancel` (D2); GET and health never ask, so they pay nothing for the disconnect watch.

- **Shutdown:** the owner ends the wait with the truthful reply:
  - Authority, task not yet in `queue_`: 503 `{"error":"stopping"}`, as a refusal before queueing
    is today (`service.cc:791-793`).
  - Authority, task queued or in consensus: 503 `{"error":"outcome_unknown"}`, as the runtime's
    shutdown and expiry paths answer (`service.cc:2706-2712`, `:1780-1787`).
  - Classic, clustered, envelope added to the Raft log: the existing timeout reply, 503 with
    reason `Transport` and a "retry" detail (`detentd.cc:340-342`, `status_for` `:32`).
  - Classic, write not yet started on the owner: the same retry reply; nothing was applied.
  - Classic, commit started: the commit finishes, unsliced (D13), and its real result is the reply;
    it fits the 5 s grace (estimate: at most 1.2 s at 16 MiB).
- **Client disconnect:** the handler keeps waiting; the wait is bounded by D8. Today's deferred
  reply treats EOF as no disconnect, and so does the owned stream's monitor
  (`http_asio.h:404`), so a client that half-closes after sending still gets its outcome.

A Shutdown cancel also sets the Distributor's `stopping_` atomic, which the owner reads before it
starts each write, so writes queued behind a long commit are answered at once rather than each
committed in turn. The command still commits when consensus has it, as it does today. A classic
waiter erases its pending slot on its reply.

Shutdown order: in `main.cc`, `service` is destroyed before `app` (D2 lifetime 2). In `host.cc`,
`listener.stop()` (`:375`) runs before `facade.reset()` (`:379`); `listener` itself lives to the end
of the scope. `HttpAsioService::stop()` waits for every handler frame (coroutine-handlers.md,
"Shutdown"), and every hop ends inside its handler frame.

**D11. `--workers` is removed** from `detentd`: the flag, `DETENTD_WORKERS` and the config field
(`main.cc:124,177`), with `docs/USAGE.md:160`, the website CLI page and the harnesses in the same
change. A removed flag fails with an error that names the removal (Q2).

**D12. Tests drive handlers through one helper.** `detent_test::run_handler(handler, request,
writer)` runs `create_extension`, the body intake and `handle` to completion on a private
`io_context` run inside a `reactor::RunScope` (`reactor.h:138-143`), so D14's assertion fires in
unit tests exactly as on a loop. It stops that `io_context` only after `handle` completes (D2
lifetime 1). It replaces the synchronous `handle()` calls in about 30 test and benchmark sites and
`DeferredWriter` (`tests/detent_authority/test_service.cc:21`). The deferred-reply TLS test
(`tests/detent_authority/test_transport_https.cc:14-86`) tests http's own `defer()`; it becomes a
pipelined coroutine reply over TLS through an owned stream.

**D13. The classic commit has one owner and publishes immutable state (L4, Q1, C1).** A
Distributor owner, one `io_context` and one thread that are members of `Distributor`, serves only
snapshot writers: unclustered PUT and PATCH, clustered applies, and the pending map. It is not a
pool, and handlers never run on it, the same pattern as the authority runtime (`service.cc:568`,
`:1142`) and the relay owner (`host.cc:358-373`).

1. **Writes.** A PUT or PATCH hops to the owner (D2). Unclustered, the owner runs the commit and
   returns its result. Clustered, the owner registers the correlation id in its pending map, calls
   `raft_->add_command` (thread-safe: a mutex and a signal, `cluster/raft.h:178-181` at Detent's
   current cluster pin; S2 confirms at `428d57d`), and waits on the channel against a 10 s timer;
   the timeout is the existing reply (`detentd.cc:306`, `:340-342`).
2. **Applies.** `on_raft_apply` (Bus loop) posts the envelope through a strand on the owner
   executor, so applies run in log order, then returns. The owner commits it and, when the
   pending map has its correlation id, sends the result on that channel. The commit leaves the Bus
   loop, which today runs it beside Raft heartbeats. Raft keeps its log in memory and does not
   depend on apply completion (`raft.h:234-237`).
3. **Commit.** The commit is a coroutine that never suspends. It resolves against the owner's own
   `current_snapshot_` with no lock, since the owner is the only writer; this keeps PATCH's merge
   race-free (`detentd.cc:183-186`). It serializes, persists before publishing (the invariant at
   `:194-204`), diffs and compresses, then publishes.
4. **Publish.** One `atomic_shared_ptr<const Published>` (Detent pins `atomic_shared_ptr`, used at
   `include/detent/snapshot_ref.hh:19`) holds the version, the ETag, the body and its gzip and
   zstd forms, the last diff and its encodings, the diff history, and the composed-diff cache for
   that version. The publish is one store. `snapshot_mutex_` goes.
5. **Reads.** GET and health load the pointer and never wait on a commit. A GET copies only the
   negotiated encoding, outside any lock, where today it copies all three under the shared lock
   (`detentd.cc:583-591`). The composed-diff cache moves into `Published`, so an entry always
   describes the version it was composed against; a cache shared across versions could take a
   stale insert between a GET's pointer load and its generation read.
6. **Measurement (C1).** The owner logs each commit's duration. The lead records the 560 KB and
   16 MiB commit times in S3, replacing D6's estimate. Slicing is reconsidered only if the commit
   exceeds a quarter of the shutdown grace (1.25 s), or if latency-sensitive work moves onto the
   owner. Slicing would need streaming compressor interfaces in place of `http::encode`
   (`detentd.cc:384-393`).
7. **Lifetime.** `~Distributor` stops the Bus first (`detentd.cc:113-118`), so no apply is posted
   after it; then it releases the owner's work guard and joins. The pending map and its channels
   are declared after the owner's `io_context`, so they die first. Blocking file I/O in
   `persist_locked` runs on the owner, which is acceptable there.

**D14. Blocking points assert they are off the loops (L5).** `detent::assert_off_loop()` is
`assert(!reactor::inside_run())`, active in the test and sanitizer builds (no `NDEBUG`).
`reactor::inside_run()` is true only on threads inside a reactor's `run()` or a `RunScope`
(`reactor.h:135-143`, `:193`); the authority runtime and the Distributor owner run plain
`io_context`s, where it is false. Every blocking point Detent owns calls it:

1. The Distributor commit and its entry points on the owner (`resolve_and_commit`, `try_accept`,
   `try_accept_patch`).
2. `persist_locked` and `read_file`.
3. Any remaining `std::future` wait or condition-variable wait in `src/detentd`,
   `src/detent_authority` and `src/detent_relay`.

The cluster Bus is a `reactor::UdpServer` (`cluster/bus.h:24,123`), so the assertion also fires if
a commit ever runs on the Bus again.

## User-visible changes

| Change | Who sees it |
|---|---|
| Oversize PUT/PATCH and authority bodies get 413 `Payload Too Large` (text/plain, close) instead of a dropped connection; a `Content-Length` over the cap is refused before any body byte | Publishers sending > 16 MiB or > `request_bytes` |
| The old offload 503 (`Service Unavailable`, keep-alive) is gone; classic write-slot refusal is 503 + `Retry-After: 1` before the body | Classic publishers past 1024 pending writes |
| Authority reply over budget or size: JSON 503 (`outcome_unknown` for commands, `admission_full` for reads) or 413 `reply_bound` instead of a close (D9) | Publisher readers and writers |
| Authority `GET /v1/health` with a body: 400 JSON `health_body_forbidden` instead of a dropped connection, as the relay answers | Non-conforming clients |
| `CONNECT` is answered by the existing routing (classic 404 or 405; authority 401, or 400 with a valid token; relay 404) and the connection closes; after HTTP/1.0 with `Transfer-Encoding` the connection closes; trailers no longer reach `header()` | Non-conforming clients |
| http-parser's strict mode drops requests with a bare LF, obs-fold, or a non-`chunked` `Transfer-Encoding`, without a reply | Non-conforming clients; Detent's own clients are checked in T7 |
| `stop()` answers waiting requests by the outcome contract (D10) and forces stalled sockets after 5 s | Clients during shutdown |
| `--workers`, `DETENTD_WORKERS` and the config field `workers` are gone (Q2) | Operators, `docs/USAGE.md:160`, `website/.../cli/detentd.md`, test harnesses |

Not changed: the receive window and idle keep-alive (both T, D8); a client that half-closes after
sending still gets its outcome (D10).

## Limits

- A handler that later gains heavy synchronous work stalls its whole loop. D14 catches it only at
  the blocking points that assert, and T3's lag probe only on exercised paths.
- D4 relies on one request per connection. If http ever served concurrent requests on one
  connection, its text/plain 503 could reach the delivery client. T4 pins the invariant.
- The authority parses up to `request_bytes` on the loop: an estimated 50-150 ms per maximum
  request in a 16 MiB configuration (S4 times it).
- The composed-diff GET stays on the loop: an estimated 0.3-0.8 s at 16 MiB, once per base version
  per publish (Q5).
- A 16 MiB classic commit delays the next writer by an estimated 0.4-1.2 s (writes are serial
  today, `detentd.cc:181`).
- The owner's apply queue holds the envelopes Raft has committed and the owner has not yet
  applied. Raft holds the same entries in its in-memory log, so the backlog's memory at most
  doubles; it is bounded by the cluster's write rate, which the write slots (D3) bound per node.
- `Request::cancel` keeps only the first reason (`http_message.h:149-150`). A client that
  disconnects before `stop()` leaves its handler waiting for the outcome deadline plus the 1 s
  backstop (6 s at the default T), which can exceed the 5 s grace for that one request.

## Designs this depends on or changes

1. **PLAN W2** (`Detent:7348e5b`): W2 keeps Detent's JSON refusals (Q3); `in_flight_limit` is a
   backstop (D4) and `BodyStream` is unused (D5).
2. **[HTTP_LOGGING_ARCHITECTURE](../HTTP_LOGGING_ARCHITECTURE.md):** the offload 503 and deferred
   replies it covers go away. The authority is not wrapped in `AccessLog` (`main.cc:269-275`);
   classic replies stay buffered, so only owned streams need its completion observer.
3. **[ASYNC_AUTHORITY_ARCHITECTURE](../ASYNC_AUTHORITY_ARCHITECTURE.md):150:** its deferred-reply
   choice for the publisher path is replaced by D2's hop and D8's owned stream.
4. **[AUTHORITY_TRANSPORT](../AUTHORITY_TRANSPORT.md):55:** its contract (outcome deadline plus 1 s
   to write) holds; the mechanism becomes an owned stream (D8).
5. **[RELAY_ARCHITECTURE](../RELAY_ARCHITECTURE.md):162:** "HTTP intake workers" become the loop.
6. **[src/detentd/CLUSTERING.md](../../src/detentd/CLUSTERING.md):** applies run on the Distributor owner, in log order (D13).
7. **[SHARED_LIBRARY_DECISIONS](../SHARED_LIBRARY_DECISIONS.md#coroutine-drivers-for-journal-and-consensus-november-2):**
   W2 does not touch journal or consensus; their coroutine drivers are M1b.
8. **W3:** its reactor, http-parser, http, http-log, cluster and consensus pins land with this
   work (S2).
9. **W4:** independent of this design.

## Gate rows

Builds and suites run under `quiet light` with at most 4 jobs. Configure with
`-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON`. The two sanitizer builds are separate:
`build-asan` (`-fsanitize=address,undefined`) and `build-tsan` (`-fsanitize=thread`), both with
Homebrew LLVM and without `NDEBUG` (C6).

| Row | Target | Procedure |
|---|---|---|
| T1 Hop bridge | (a) Completion settled before and after the hop starts, from the owner thread; (b) a Shutdown cancel, emitted before the hop starts and during the wait, reaches the owner and the truthful reply is written within the 5 s grace; (c) a late owner completion after the loop-side wait ended is clean; (d) every channel is destroyed before its owner's `io_context`; (e) a hop after an authority runtime failure answers 503 `stopping` (D2 lifetime 3); (f) mutant: dropping the explicit slot binding makes T6's shutdown case exceed the grace | `ctest -R hop` in native, `build-asan` and `build-tsan`; (f) by reverting the binding on a scratch commit |
| T2 Writes never block reads | `--reactors 1`. Unclustered: a test hook holds the owner mid-commit (after resolve, before publish) on a latch; `GET /v1/snapshot` and `GET /v1/health` answer with the old version before the latch opens; then the new version. Clustered without a quorum: a PUT waiting for consensus, then `GET /v1/health` on the same loop, which answers before the PUT's 503 | `tests/detentd/test_distributor_basic.cc` and `test_raft_cluster.cc` new cases |
| T3 No loop blocking | (a) D14's assertion at every blocking point, exercised by the suites in the test and sanitizer builds; (b) with `enable_lag_probe()`, T2's and T6's fixtures keep `reactor(i).lag().snapshot().max_us` under 250,000 while a 10 s wait is pending; (c) mutant: moving the classic commit back inline in the handler fails (a) and (b) | Fixtures assert the lag; (c) on a scratch commit, reverted |
| T4 Admission | Each Detent cap (authority requests, per producer, bytes, subscription lanes, relay lanes, classic write slots) refuses with Detent's bytes before the client sends a body byte; every account returns to zero, and `reserved_reply_bytes` returns to its baseline after a late completion without waiting for the task to retire; a TLS client that does not read keeps `reserved_reply_bytes` until its write ends; no request ever sees http's gate (1025 concurrent clients against the authority) | `test_service.cc` and `relay_network_fixture` cases, in native, `build-asan` and `build-tsan` |
| T5 Body caps | Each D5 row: cap accepted and cap + 1 refused, with `Content-Length` and chunked; the 20 MiB classic PUT gets 413, or a reset if the client's write fails first | `test_distributor_basic.cc:112` rewritten; authority and relay cases |
| T6 Cancel and shutdown | `stop()` with waiting publishers: a task not yet queued gets 503 `stopping`, a queued task 503 `outcome_unknown`, a clustered classic waiter the 503 retry reply, an unclustered commit in progress its real result, and 1024 classic writes queued behind a held commit are answered within the grace; disconnect while waiting: the handler waits, accounts reach zero, `reserved_reply_bytes` returns to baseline, and a later runtime settle is clean; a half-closed client receives its outcome; over-budget command gets `outcome_unknown`, over-budget read `admission_full` | Authority and classic fixtures, in native, `build-asan` and `build-tsan` |
| T7 Wire behavior | CONNECT, HTTP/1.0 with `Transfer-Encoding`, bare LF, oversize, authority health with a body: the replies in the user-visible table; requests captured from `detent_http`, the delivery client and `https_client` replay through strict http-parser with no rejection | New `test_http_conformance.cc` |
| T8 Suites | Full native suite, Python and Rust bindings, `build-asan`, `build-tsan`, and the real-process fixtures (`tests/detent_system`, `test_raft_cluster`, `relay_network_fixture`, `test_multivoter.py`); hosted CI green on every job; T10's greps are empty | `ctest -j4` per build; `pytest tests/detent_system` |
| T9 Latency A/B (lead) | No regression beyond the A/A spread for `detentd_fanout_bench` GET, `read_response_bench`, classic PUT, authority publish, `detent_read_bench`, and the managed first read (sub-millisecond target held, PLAN criterion 6) | `cpp-libraries/tools/ab_compare.py`, old binary from `Detent:aa00b0e` against the branch, with an A/A control, under `quiet strict` holds of at most 10 min |
| T10 Removal | `git grep -nE 'should_offload\|DeferredReply\|DeferredOptions\|->defer\(\|\.defer\(\|user_data\|BodyReader' -- src include tests benchmarks` and `git grep -nE -- '--workers\|DETENTD_WORKERS' -- src/detentd docs website` are empty | CI step |
| T11 Deadline timeline | Authority, T = 1 s, plain TCP and TLS: a body trickled to just under the receive bound, then an outcome held to its deadline: the client receives the whole 503 `outcome_unknown`; the same with an 8 MiB read reply: all bytes arrive; an idle keep-alive connection closes after T | A fixture on the shape of `.scratch/arch-review/d8_deadline_probe.cc`, in native and `build-tsan` |

## Owner questions

Q1-Q4 are answered as the architect recommended; the owner direction of 2026-11-02 adopts them.

**Q1. Where the classic commit runs.** *Answered:* one Distributor owner `io_context` with the
commit as an unsliced coroutine (D13). The off-lock publish is a decision for both modes, not an
option detail.

**Q2. `--workers`.** *Answered:* removed outright, with an error that names the removal (D11).
Accepting and ignoring it is a shim (PLAN rule 2); repurposing it changes its meaning silently.

**Q3. http's refusals and Detent's JSON contract.** *Answered:* http's gate stays unreachable and
Detent's JSON admission is the cap (D4). PLAN W2 records it (`Detent:7348e5b`).

**Q4 (for the lead). Removing `Request::user_data` and `defer()` from http.** *Answered:* yes,
coordinated with `xapiand-74`, after W2 lands. No consumer uses them (Xapiand `8ef0f8633`, strata
`fe6d3ca`, prism `da169a9`, http-log `431b878`, per the review). A follow-up, not a W2 step.

**Q5 (for the lead). The composed-diff GET** (D6). It compresses up to snapshot size on the loop,
an estimated 0.3-0.8 s at 16 MiB, once per base version per publish.

1. Keep it on the loop in W2 and measure it in M3 with the first-read work.
2. The owner precomputes the composed diffs for every base in the history at each publish: up to
   64 composes and compressions per commit.
3. A GET that misses the cache hops to the owner, where it can wait behind a commit.

*Recommendation: option 1. It is inherited, cached per base version, and the other options
either multiply the commit's work or put GETs behind commits.*

## Follow-ups (not W2 steps)

Owner-loop internals, after W2:

1. The authority runtime takes HTTP tasks from `queue_` under `mutex_` on a timer tick
   (`service.cc:2370-2387`, `:1612-1628`, `:2527-2531`). The hop already runs on the runtime, so it
   can enqueue there and wake the runtime directly.
2. The relay owner's `turn` re-arms on a 1 ms timer (`relay/host.cc:340-346`).

Milestone M1b (out of scope here): the coroutine drivers for journal and consensus, then
Detent's owner loops on them ([SHARED_LIBRARY_DECISIONS](../SHARED_LIBRARY_DECISIONS.md#coroutine-drivers-for-journal-and-consensus-november-2)).

Library follow-ups (W2 needs none of them):

1. An additive `Request::deadline()` in http, so a handler can bound its outcome wait by the
   remaining write budget.
2. Removing `Request::user_data` and `defer()`/`DeferredReply` from http (Q4), with `xapiand-74`.
3. Persist through completion's async file operations on the Distributor owner.

## First steps

Every commit on the branch builds and passes the suites it touches (C3).

| Step | Content | Blocked on |
|---|---|---|
| S1 | On the current pins: the hop helper (D2) with T1 (a)-(d) driven by a plain `cancellation_signal`; `detent::assert_off_loop` (D14) | none |
| S2 | Pins: http-parser, reactor, http, http-log and cluster heads, plus consensus where W1 has landed; `HttpAsioService` constructors and the one-value caps (D4); a mechanical coroutine conversion of all three handlers, still blocking inside; `run_handler` (D12); the relay migration (D3, D5); the `Request::on_cancel` bridge; T7 | W1 for consensus |
| S3 | Classic: the Distributor owner and published state (D13), hops for PUT/PATCH, applies posted to the owner, write slots, `set_max_body`, D14's calls; T2, T5, classic T6; the commit-duration log for the lead's C1 measurement | S2 |
| S4 | Authority: the runtime `io_context` as a `Service` member (D2 lifetime 3), `create_extension`, the hop, owned-stream replies (D8), reply budget (D9), cancel (D10); tests off `DeferredWriter`; T1 (e), T4, T6, T11; the parse timing for C2 | S2 |
| S5 | `--workers` removal and documentation (D11); T10 | S2 |
| S6 | T3 with its mutant, T8 under both sanitizers, then the lead's T9 | S3-S5 |
