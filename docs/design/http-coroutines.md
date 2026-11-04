> Status: DRAFT (2026-11-03) Design for workstream W2 of [PLAN.md](../PLAN.md) milestone M1 "on the tip", revision 3: retargets the handlers and the owner hop from `asio::awaitable`, `co_spawn` and channels to http's task API (http `docs/design/task-handlers.md` revision 2) and folds in the re-check's N1-N6 ([reviews/http-coroutines-design.md](../reviews/http-coroutines-design.md), "Re-check, 2026-11-02"). Ledger: [status/http-coroutines.md](../status/http-coroutines.md).

# HTTP coroutines in Detent

Detent's three HTTP handlers run as `task::task<void>` on the reactor loops, with no worker pool and
no blocking wait on a loop, and every other `asio::awaitable` in Detent moves to `task::task`.

- Workstream: W2 HTTP coroutines (designer). Branch `design/http-coroutines`, merged with
  `Detent:6a31280`; `src`, `include`, `tests`, `benchmarks` and `cmake` are unchanged since
  `Detent:aa00b0e` (`git diff --stat aa00b0e HEAD` over them is empty), so Detent line numbers hold.
- Read for this revision: http `docs/design/task-handlers.md` revision 2 (`http:209ceef`) and its
  review `docs/reviews/task-handlers-rev2-design.md` (`http:1ae1390`); task `docs/API.md`,
  `task.hh`, `task_asio.hh` and `docs/design/cancellation.md` on branch `cancellation-c2`
  (`task:cbfee34`); completion `include/completion/asio_completion.h` (`completion:e2c6474`).
  http head facts are at `http:679c0f8`, as in revision 2 (exports in `.scratch/arch-review/`).

## Abstract

http's step H1 makes `HttpHandler::handle` return `task::task<void>` and awaits it as a child task
on the connection's loop (task-handlers D1, D2). Detent's handlers are the classic distributor
(`detentd::Distributor`), the durable authority (`detent::authority::Service`) and the relay
(`detent::relay::Service`). Today they block a worker thread while consensus decides, park a reply
in `http::DeferredReply`, and pass admission through `Request::user_data`. This design:

1. Reaches owner work (the authority runtime, a Distributor owner) with a child task that moves to
   the owner's `task::asio_scheduler` with `task::schedule_on`, waits there on a `task::event` in the
   per-request state the owner already keeps, bounded by a `task::deadline` linked to the service's
   stop token, and comes home at its final suspend. Detent adds no completion type.
2. Runs the classic commit unsliced on one Distributor owner and publishes immutable state through
   `atomic_shared_ptr`, so no GET or health request waits on a commit.
3. Moves admission to `create_extension`, typed as a `RequestExtension`; Detent's JSON refusals stay
   the only refusals clients see, and http's `in_flight_limit` is set so its gate never fires.
4. Answers every submitted command truthfully with at least 1 s to write the reply, keeps the receive
   window at T, and writes shutdown replies through the buffered writer (N1).
5. Moves Detent's own `asio::awaitable` code (delivery client, coordinator, completion scheduler,
   managed host, HTTPS client, authority runtime, relay owner, `fleet_load`) to `task::task`, as the
   architect ruled on http's Q2 (`task-handlers-rev2-design.md`, "Rulings").

Every CPU figure below is an estimate unless it names a measurement. The lead's A/B (T9) decides
performance.

## What changed in this revision

| Item | Revision 2 | Revision 3 |
|---|---|---|
| Handler type | `asio::awaitable<void>` | `task::task<void> handle(http::Request&, http::ResponseWriter&)` (D1) |
| Owner hop | `co_spawn(owner, work, bind_cancellation_slot(...))`, a capacity-1 `experimental::channel`, `\|\|` with a `steady_timer` | Child task, `schedule_on`, `task::event` in existing per-request state, `task::deadline` with a `cancel_callback` (D2) |
| Shutdown signal | `Request::on_cancel` bridge on `CancelReason::Shutdown` | Detent's own `task::cancel_source` per service, requested by `begin_shutdown()` before the HTTP service stops; Detent never reads `request.cancel_token()` (D10) |
| Owners | An ad hoc `io_context` and thread per owner | One `detent::OwnerLoop` shape for the three owners (D2) |
| Teardown | Declaration order | task's four phases across http's reactors and Detent's owners (D15) |
| Other `asio::awaitable` code | Out of scope | Steps S6-S9 (D16), T12 |
| N1 | Shutdown replies through an owned stream | The buffered writer (D8, D10, T6) |
| N2 | `assert_off_loop` in S1 on reactor `fe8c5ce` | In S2, after the reactor pin (D14) |
| N3 | `start(metadata, length)` | `start(metadata, length, request.keep_alive)`; T11 sends a second request (D8) |
| N4 | Implicit | `~Service` releases the runtime's work guard only after `stopping_` is set (D2, S4) |
| N5 | T10's second pattern could never be empty | Scoped to `src/detentd website docs/USAGE.md` (T10) |
| N6 | Cancel wake-up unproven | T1(b) is S1's first test, now on task's event and deadline (S1) |
| Unclosed limit | First-reason swallow; classic 10 s | Closed by D10's service stop source; T6 "disconnect, then stop" |
| Re-check L4 notes | Cache type unnamed; history copied | `Published` holds a `generation_cache::Cache` and shares history entries by `shared_ptr` (D13) |

No mechanism here is new beyond what the architect reviewed in revision 2 and in task-handlers
revision 2. The owner hop keeps its shape (hop, wait on the owner, truthful reply, come home) on task
primitives; D10 adopts the re-check's own `begin_shutdown()` proposal; D15 is task D8's four phases
applied to Detent's owners, which TH2-R1 anticipates ("Detent's owner-loop tasks"). The fitting review
is a scoped re-check of N1-N6, D2, D10 and D15, not a new architect review.

## What Detent runs on the removed path today

| Site | Today | Why it cannot stay |
|---|---|---|
| `src/detentd/detentd.cc:125`, `detentd.hh:123` `should_offload` | PUT/PATCH on workers, GET inline | Gone at http head |
| `src/detentd/detentd.cc:310-347` `submit_command` | Blocks the worker on `pending_cv_.wait_for` for up to 10 s (`:306`, `:333`) | A blocking wait |
| `src/detentd/detentd.cc:181`, `:488`, `:544`, `:625` | Commit holds `snapshot_mutex_` exclusively; GET and health take it shared on the loop | A loop waits on a commit (L4) |
| `src/detentd/detentd.cc:349-365` `on_raft_apply` | Clustered commit on the cluster Bus loop | A reactor loop does the commit |
| `src/detentd/detentd.cc:146-171` `CappedBodySink` | 16 MiB cap; over it, the connection drops | Duplicates `set_max_body` |
| `src/detentd/main.cc:244`, `:278`, `:284` | `TimedConnectionFactory`; `HttpAsioService(handler, reactors, workers, 1024, ...)` | Types and constructors merged (task-handlers D1) |
| `src/detent_authority/service.hh:55-56` | `void handle(const Request&, ...)`; `should_offload` | Signature changed; offload gone |
| `src/detent_authority/service.cc:675-700` | `admit()` into `user_data`, then a capped sink | Replaced by `create_extension` |
| `src/detent_authority/service.cc:964-1013` | `response.defer()`; the runtime thread completes it | Detent stops using `defer()` (PLAN criterion 3) |
| `src/detent_authority/service.cc:1016-1030` | Recording-writer fallback: `future.wait_until` | A blocking wait |
| `include/detent_relay/service.hh:40-41`, `src/detent_relay/service.cc:352-421` | Offload; `user_data` carries the lease | As above |
| `src/detent_relay/host.cc:243-245` | `HttpAsioService(*facade, 2, 2, connections, tls_connection_factory_timed(...), connections)` | Constructor and factory merged |

At head, http keeps `Request::user_data` (`http_message.h:126`) and `DeferredReply`
(`http_deferred.h:251`); task-handlers D1 leaves the writer side unchanged. Detent stops using both
(PLAN criterion 3); removing them from http is a follow-up (Q4).

### Detent's own `asio` coroutine code

`git grep -nE 'asio::awaitable|co_spawn|use_awaitable|awaitable_operators|this_coro' -- src include
tests benchmarks` at `Detent:6a31280`, by file, with the line of every match:

| File | Lines | What it is | Step |
|---|---|---|---|
| `include/detent_transport/https_client.hh` | 4 11 13 18 19 22 51 67 76 79 82 84 102 | HTTPS health client; `\|\|` deadline race (`:82-84`); `co_spawn` in the sync wrapper (`:102`) | S6 |
| `include/detent_delivery/client.hh` | 42 103 104 107 109 110 115 | Public `open_session`, `next_session`, `close_session` | S6 |
| `src/detent_delivery/client.cc` | 7 141 144 174 178 185 191 197 213 230 235 237 247 254 269 292 297 301 303 312 473 495 505 541 563 620 630 640 650 | Delivery transport; `\|\|` deadline race (`:301-303`) | S6 |
| `src/detent_delivery/client_probe_internal.hh` | 10 14 | `ProbeAccess::next`, `reference` | S6 |
| `src/detent_delivery/completion_scheduler.cc` | 80 85 89 94 108 119 | Owner-executor check (`:89`), disabled cancellation (`:94`); awaits the completion driver (`:122`) | S7 |
| `include/detent_delivery/completion_scheduler.hh` | 16 36 | `CompletionPort::run_one`, `run_owned` | S7 |
| `src/detent_delivery/coordinator.cc` | 17 138 139 146 149 152 164 171 175 253 312 500 518 531 550 631 638 726 770 774 775 791 806 848 851 853 873 875 914 915 916 1165 1172 1174 1175 1178 | Coordinator loops; `co_spawn` bound to `network_cancel` (`:851-853`, `:873-875`, `:914-916`); `&&` (`:1178-1179`); awaits the driver (`:674`) | S8 |
| `include/detent_delivery/coordinator.hh` | 81 89 | `Coordinator::run`, `run_owned` | S8 |
| `src/detent_agent/managed_host.cc` | 8 26 28 90 107 109 169 | `monitor`; `supervise` with `&&` (`:109-111`); `co_spawn(..., use_future)` (`:169`) | S8 |
| `src/detent_agent/managed_host_internal.hh` | 9 | `monitor` declaration | S8 |
| `src/detent_relay/host.cc` | 362 | `co_spawn(owner, coordinator->run(), ...)` on the relay owner | S8 |
| `src/detent_authority/service.cc` | 1111 1137 1142 1161 1612 1625 1628 2527 2531 2572 2588 2597 2604 2615 2636 2639 2651 2669 2673 2681 2697 2701 2704 2716 2725 2729 2732 2737 | Authority runtime coroutine (`:1137-1161`), its yields and timer waits; `co_spawn` of the completion driver (`:1111`) | S4, S7 |
| `benchmarks/fleet_load.cc` | 158 173 178 186 193 197 200 201 204 205 208 209 212 234 238 239 267 281 292 317 406 408 418 435 440 515 517 523 562 563 580 593 620 627 791 | Load generator; also `experimental::channel` (`:47`) and `cancellation_signal` (`:27`, `:561`) | S9 |
| `tests/detent_authority/test_transport_https.cc` | 120 | `https_health` driver | S6 |
| `tests/detent_authority/test_distribution_service.cc` | 243 | `co_spawn` of a delivery call | S8 |
| `tests/detent_authority/test_transport_peers.cc` | 113 130 164 175 191 204 210 226 251 255 | Peers exercise; `co_spawn` of the driver (`:130`) | S7 |
| `tests/detent_delivery/test_client.cc` | 215 267 286 327 392 397 | Client tests | S6 |
| `tests/detent_delivery/test_probe_client.cc` | 4 133 142 148 153 170 179 182 185 204 211 223 225 226 227 232 233 238 240 244 247 317 | Probe client tests | S6 |
| `tests/detent_delivery/test_reconnect_cancel.cc` | 16 17 22 25 32 73 74 | Cancelled reconnect | S6 |
| `tests/detent_delivery/bootstrap_tls_fixture.hh` | 18 21 59 64 81 92 100 | TLS bootstrap fixture | S6 |
| `tests/detent_delivery/test_bootstrap_retention.cc` | 84 | Retention test | S6 |
| `tests/detent_delivery/relay_network_fixture.hh` | 47 49 69 77 100 101 102 109 110 126 152 172 188 259 274 281 296 322 | Relay network fixture (client and coordinator halves) | S6, S8 |
| `tests/detent_delivery/test_completion_scheduler.cc` | 77 84 85 86 87 96 99 107 127 | Completion scheduler | S7 |
| `tests/detent_delivery/test_coordinator.cc` | 9 235 238 244 257 269 280 316 322 324 346 356 358 360 363 383 395 400 407 408 413 884 1035 1114 1139 1177 | Coordinator | S8 |
| `tests/detent_delivery/test_preparation_deadline.cc` | 5 22 23 27 36 38 41 48 53 54 86 | Preparation deadline | S8 |
| `tests/detent_delivery/test_prepared_startup.cc` | 7 55 57 73 90 100 102 173 | Prepared startup | S8 |
| `tests/detent_delivery/test_worker.cc` | 39 42 51 61 109 112 | Worker | S8 |

Twelve `src` and `include` files: the eleven with `asio::awaitable` that the architect counted, plus
`src/detent_relay/host.cc`, which only calls `co_spawn`. One benchmark and fifteen test files follow.
The completion driver Detent awaits at `service.cc:1111`, `completion_scheduler.cc:122` and
`coordinator.cc:674` is `kronuz::journal::AsioCompletionDriver::run`/`run_one`, which returns
`asio::awaitable<void>` (`completion/include/completion/asio_completion.h:101-112` at
`completion:e2c6474`); Q6 asks where it moves.

## Decisions

**D1. Every handler is a task; synchronous work stays synchronous.** `handle` returns
`task::task<void>` and takes `http::Request&`; the classic `Router` routes return `task::task<void>`
(task-handlers D1). http awaits the handler as a child started by symmetric transfer, so a handler
that never suspends costs no post (task-handlers D2). GET snapshot, health, and relay or authority
intake never suspend. No callback that outlives `handle` captures the `Request` or the
`ResponseWriter`. Detent never calls `request.cancel_token()` (D10), so its handlers never engage
http's disconnect watch.

**D2. Owner work is a child task that moves to the owner, waits there, and comes home (L1).** Where a
handler starts work that belongs to an owner, it awaits a child task:

```cpp
// The handler, on its loop; the reply comes back on the loop.
Reply reply = co_await detent::owner_submit<Reply>(
	owner,
	[&] { return accept(request); },                        // on the owner: returns the shared per-request state
	[&](State& s, task::cancel_reason why) { return reply_for(s, why); },  // on the owner: the truthful reply
	backstop_at, stop_.token());
```

`owner_submit` (`include/detent/owner_loop.hh`) takes two callables, `accept` and `finish`, and no
`Pending` type, because each owner keeps its own per-request state: the authority's `Task` (settled once
on the runtime thread under `task->settled`) and the classic `Write` (D13); each has a `task::event done`
and the reply, and `finish(state, reason)` builds the truthful reply from the state and the reason the
wait ended with (`none`, `shutdown`, `deadline`). The owner's completion writes the reply, sets `settled`, then calls
`done.set()`. The waiter resumes on the owner thread, so only the owner thread touches the reply
state. The callback only sets the event, from whichever thread requests the token: the backstop's
timer on the owner, or `begin_shutdown()` (D10).

What task provides (`task:cbfee34`):

1. It resumes on the loop. A child that completes on another scheduler posts its awaiter home
   (`task.hh:481-491`). task-handlers D9 allows a child that moves with `schedule_on`; the handler
   touches the writer only after it is home.
2. An exception in the child rethrows in the handler (`task.hh:495`).
3. A wake from another thread is safe. `cancel_callback` runs on the requesting thread with
   `std::stop_callback`'s lifetime (`task.hh:1057-1083`); `event::set` is callable from any thread and
   resumes each waiter through the scheduler it suspended on, never inside `set` (`task.hh:1195-1199`).
4. It settles once. `event::set` is idempotent (`task.hh:1219-1221`); a completion racing a stop or a
   backstop leaves one reply: the settled one when published, otherwise D10's.

Owners. The three owners (the Distributor owner, D13; the authority runtime; the relay owner,
`host.cc:352-373`) share one shape, `detent::OwnerLoop`: an `asio::io_context`, a
`task::asio_scheduler` on it, a work guard and one thread, with `stop()` (release the guard, stop,
join), `close()` and `destroy_roots()` for D15. It is not a `reactor::Reactor`: blocking file I/O runs
on owners, and `reactor::inside_run()` must stay false there (D14).

Lifetimes:

1. A handler's child frame belongs to its connection's root on the loop's scheduler. D10 ends every
   owner wait before the HTTP service stops, so no frame is suspended on an owner when the loops stop;
   D15 covers the frames a forced stop leaves.
2. The authority runtime's `io_context` dies with the local `Runtime` in `Service::run`
   (`service.cc:1137-1152`) while HTTP still serves, and a hop after that is a use after free. It
   becomes an `OwnerLoop` member of `Service`, declared before `queue_`. `~Service` sets `stopping_`,
   then releases the work guard, then joins (N4): released earlier, `io.run()` can return with tasks
   still queued; never released, the join hangs. After a runtime failure the owner keeps running, and a
   submission is refused with 503 `stopping` before it enqueues (`service.cc:791-793`).
3. Each event is awaited by one frame and owned by per-request state that the waiter and the owner hold
   through `shared_ptr`, which satisfies task's ownership rule (task `cancellation.md` D8, phase 3). A
   late completion sets an event nobody waits on.

Cost, estimated: per write request, one post to the owner (`schedule_on`), one resume post from
`event::set`, one post home, one timer start and one callback registration (registration allocates
nothing, `docs/API.md`). GETs and health hop nowhere. T9 measures it.

Not selected:

- **Waiting on the loop.** `handle` has no reactor to build a `task::deadline` on (task-handlers D1),
  and the classic pending map is the owner's.
- **A Detent one-shot (`Outcome<T>`).** The event lives in the state each owner already keeps per
  request; a generic value-carrying type would duplicate it.
- **`request.cancel_token()`.** First-wins keeps a disconnect's reason and drops a later shutdown
  (task-handlers D5), and a disconnect must not end the wait (D10).

**D3. Admission happens in `create_extension` and lives in a typed `RequestExtension`.** At head, the
headers-complete hook enters http's gate (`http_asio.h:752`), calls `create_extension` (`:755`), and
only then chooses the body intake; task-handlers D1 does not change them. Each service returns its
own extension, and `handle` reads `request.ext<T>()`:

| Service | Extension holds | Refusal (unchanged bytes) |
|---|---|---|
| Authority | `shared_ptr<Admission>` (`service.cc:589` `admit`), later the `ReplyAdmission` (D9) | 401/403/503 JSON `RequestRejected` |
| Relay | `shared_ptr<Impl::Admission>` with its lane lease | 403/404/503 JSON |
| Classic | A write slot for PUT/PATCH, at most 1024 per process (today's `queue_limit`) | 503 `Service Unavailable` + `Retry-After: 1`, before any body byte |

The handler-side fallbacks `request.user_data ? ... : admit(request)` (authority `service.cc:955`, relay
`service.cc:416-421`) go, so a request has one admission path. The extension is a member of the
`Request`, which lives in the connection frame through `respond()` and an owned stream's write
(`http_asio.h:964-969`, `:793`, `:624`; task-handlers D2 keeps it there), so anything the extension
holds is held through the write.

**D4. http's gate never refuses a Detent request.** The delivery client accepts control replies only as
`application/json` with status 400, 401, 403, 409, 410, 429 or 503
(`src/detent_delivery/response.cc:173-186`); http's gate answers in `text/plain`. Each service builds
its connection cap and `in_flight_limit` from one value, passed to
`HttpAsioService(handler, reactors, in_flight_limit, connection, maximum_connections)`
(task-handlers D1):

- Authority: one `kPublisherConnections = 1024` in `main.cc` for both (today two literals, `:278`).
- Relay: `options.connections` for both (`host.cc:243-245`).
- Classic `detentd`: `in_flight_limit` 0 (no cap); its GETs are not admitted today either.

A connection holds at most one request between headers-complete and the settled response, and a gate
lease spans exactly that (`http_asio.h:747,752,921,966`; task-handlers "The L3 constraints" keeps the
lease in the connection frame), so a per-reactor limit equal to the connection cap cannot bind. T4
proves it.

**D5. Body caps: http's cap where bytes land in `Request::body`, Detent's sink where they land in an
accounted lease.**

| Route | Accepted today | After | Mechanism after |
|---|---|---|---|
| `detentd` PUT/PATCH `/v1/snapshot` | ≤ 16 MiB (`detentd.cc:146`); over it, the connection drops | ≤ 16 MiB; over it, 413 | `set_max_body(detent::kDefaultMaxBytes)` (`json_schema.hh:20`) |
| Authority `/v2/open`, `/v2/close`, `/v2/transactions`, `/v2/read`, `/v2/result`, `/v2/distribution` | ≤ `request_bytes` (default 256 KiB, at most 16 MiB, `state.cc:313`); over it, the connection drops | Same bound; over it, 413 | `set_max_body(request_bytes)`; `Sink` (`service.cc:180`) removed |
| Authority session open/next/close | ≤ lane `body_limit`, 413 JSON `body_bound` | Unchanged | `SubscriptionSink` into the lease, read from the extension |
| Relay session routes | ≤ lane `body_limit`, 413 JSON | Unchanged | Relay `Sink` into the lease (`service.cc:376`); `set_max_body` = largest lane limit |
| Health (authority, relay) | Relay: 400 JSON `health_body_forbidden` (`service.cc:354-356`); authority: the connection drops (`service.cc:677`) | 400 JSON `health_body_forbidden` for both | Header check in `create_extension` |

The old sink accepts exactly the cap (`detentd.cc:152`); http refuses `Content-Length > cap`
(`http_request_parser.h:255`) and a chunked body past it (`:268`). Large data travels in responses
through owned streams, which `set_max_body` does not cap. `BodyStream` (task-based in http's H2) is
unused, so W2 does not wait on H2.

**D6. CPU-heavy work stays where the inventory places it (C2).** Estimates assume one core (rapidjson
DOM about 150-300 MB/s, zlib level 6 about 20-60 MB/s, zstd level 3 about 300-500 MB/s); none is
measured.

| Work | Where today | Size (estimate) | After |
|---|---|---|---|
| Classic commit (`detentd.cc:178-240`) | HTTP worker (unclustered); Bus loop (clustered) | 15-40 ms at 560 KB; 0.4-1.2 s at 16 MiB | Distributor owner, unsliced (D13); S3 logs it (C1) |
| Classic composed-diff GET (`detentd.cc:569-608`) | Loop | About 0.3-0.8 s of gzip at 16 MiB, once per base version per publish | Unchanged; Q5 |
| Authority parse (`service.cc:706`, `:849`, `:880`) | HTTP worker | ~1-3 ms at 256 KiB; ~50-150 ms at 16 MiB | Loop; S4 times it (C2) |
| Relay session parse (`relay/service.cc:422`) | HTTP worker | ~3-7 ms per MiB, bounded by `body_limit` | Loop; `parse_session_request_on_worker` becomes `parse_session_request` |
| Authority consensus, storage, encoding | Runtime thread (`service.cc:568`) | n/a | Runtime `OwnerLoop`, unchanged |
| Relay frame encoding | Relay owner (`host.cc:358`) | n/a | Relay `OwnerLoop`, unchanged |

**D7. Owned streaming stays and carries the publisher reply.** Distribution, subscription and relay
replies keep `response.stream()` and `OwnedReplyPump`. An owned stream's write is bounded by its own
deadline (`write_owned_response`, `http_asio.h:568-619`); task-handlers D7 keeps that, as a
`task::deadline` at `exchange->deadline()` linked to the reply control's token. D8 uses it for the
authority's publisher replies.

**D8. Authority deadlines: a per-phase timeline (L2, N1, N3).** With T the authority `timeout`
(default 5 s, `service.hh:46`):

| Phase | Bound | Set by |
|---|---|---|
| Idle, headers and body | T from the top of the keep-alive loop | `set_request_timeout(T)`, unchanged (`main.cc:279`); a `task::deadline` inside http (task-handlers D4) |
| Outcome | T from `handle` start; the runtime answers 503 `outcome_unknown` at that deadline (`service.cc:1780-1787`) | `options_.timeout` |
| Backstop | 1 s after the outcome deadline: `outcome_unknown` | D2's `task::deadline` on the runtime |
| Reply write | Until `max(now, outcome deadline) + 1 s`, measured when the reply is ready | `StreamOptions::reply.deadline` of an owned stream |

A reply after a submission leaves through an owned stream created when the reply is ready:
`start(metadata, length, request.keep_alive)`, one `publish(chunk)`, `finish()`
(`http_owned_stream.h:301-310`), with `chunk_bytes`, `maximum_bytes` and `reply.limits.body_bytes`
set to `reply_bytes`. `keep_alive` is the request's (N3): `start` defaults it to `false`, which writes
`Connection: close` (`http_owned_stream.h:82-83`, `:301`), while the deferred reply this replaces
keeps the connection, as the distribution path does (`reply_pump.cc:29,33`).

**Exception (N1):** a wait that ended on Detent's stop token (D10) answers through the buffered writer
(`response.send(503, ...)`, a few dozen bytes). `stop()` aborts every registered owned stream first,
and a stream started after that is closed before `start`: the re-check's probe got 0 reply bytes and
`StreamSubmit::Closed` on plain TCP and TLS, while the buffered reply reached the client on both.
Replies before a submission (refusals, parse errors, health) stay buffered under the request deadline,
as today.

`request_timeout` stays T, so a trickling client holds a slot at most T and idle keep-alive
connections live T. No timeout is added. T11 is the trickled-body row.

**D9. Reply budget (L3).** The runtime's `complete` stores the `Reply` in the `Task` and sets its event
(D2). The child runs today's budget check (`service.cc:975-1000`) on the runtime thread and returns the
reply with its `ReplyAdmission`, which the handler moves into the extension, so `reply_bytes` stays
reserved until the owned stream's write ends (D3); `ReplyAdmission`'s destructor releases it
(`service.cc:500-511`).

| Case | Command (`/v2/open`, `/v2/close`, `/v2/transactions`) | Read (`/v2/read`, `/v2/result`) |
|---|---|---|
| Over the reply budget | 503 `{"error":"outcome_unknown"}` (a same-identity retry returns the retained outcome, `AUTHORITY_ARCHITECTURE.md:81`) | 503 `{"error":"admission_full"}` |
| Over `reply_bytes` | Not reachable for a command reply | 413 `{"error":"reply_bound"}` (`service.cc:2343`) |

**D10. Shutdown is Detent's own token; a disconnect keeps waiting (L3, the unclosed limit).** Each
service that hops (the Distributor, the authority) owns a `task::cancel_source stop_`.
`begin_shutdown()` sets the service's `stopping_` atomic, then requests `stop_` with
`task::cancel_reason::shutdown`. Every Detent stop path calls it before the HTTP service stops:
`main.cc:310` before `service.stop()`, and `run_handler` (D12) in tests; the relay does not hop and
needs none. Owner waits observe only `stop_` (D2), so a client that disconnected first no longer
swallows the shutdown (the re-check's first-reason limit), and the classic 10 s wait ends at once. On
the stop token, the owner ends the wait with the truthful reply, written buffered (N1):

- Authority, task not yet in `queue_`: 503 `{"error":"stopping"}` (`service.cc:791-793`).
- Authority, task queued or in consensus: 503 `{"error":"outcome_unknown"}` (`service.cc:2706-2712`,
  `:1780-1787`).
- Classic, clustered, envelope added to the Raft log: the existing 503 with reason `Transport` and a
  "retry" detail (`detentd.cc:340-342`, `status_for` `:32`).
- Classic, write not yet started on the owner: the same retry reply; nothing was applied.
- Classic, commit started: the commit finishes unsliced (D13) and its real result is the reply
  (estimate: at most 1.2 s at 16 MiB, inside the 5 s grace).

The owner reads `stopping_` before it starts each write, so writes queued behind a long commit are
answered at once. A command consensus already holds still commits, as today.

A client disconnect does not end the wait; D8 bounds it. Detent never reads `request.cancel_token()`,
so a half-closed client that sent its request still gets its outcome, as the deferred reply and the
owned stream's monitor treat EOF today (`http_asio.h:404`).

**D11. `--workers` is removed** from `detentd`: the flag, `DETENTD_WORKERS` and the config field
(`main.cc:124,177`), with `docs/USAGE.md:160`, the website CLI page and the harnesses in the same
change. A removed flag fails with an error that names the removal (Q2).

**D12. Tests drive handlers through one helper.** `detent_test::run_handler(handler, request, writer)`
builds a `reactor::Reactor`, spawns `create_extension`, the body intake and `handle` on
`reactor.scheduler()` (task-handlers R2), and runs the loop on the test thread until `handle`
completes, so `reactor::inside_run()` is true and D14 fires as on a real loop. It then runs D15's
phases. It replaces the synchronous `handle()` calls in about 30 test and benchmark sites and
`DeferredWriter` (`tests/detent_authority/test_service.cc:21`). The deferred-reply TLS test
(`tests/detent_authority/test_transport_https.cc:14-86`) becomes a pipelined task reply over TLS
through an owned stream. No test calls `task::sync_wait` on a loop thread (task-handlers Limit 2).

**D13. The classic commit has one owner and publishes immutable state (L4, Q1, C1).** The Distributor
owner is an `OwnerLoop` member of `Distributor` that serves only snapshot writers: unclustered PUT and
PATCH, clustered applies, and the pending map. Handlers never run on it.

1. **Writes.** PUT and PATCH hop to the owner (D2). Unclustered, the owner runs the commit and returns
   its result. Clustered, the owner adds a pending entry (`shared_ptr`, with the event and the reply)
   under the correlation id, calls `raft_->add_command` (thread-safe, `cluster/raft.h:178-181` at
   `cluster:428d57d`), and waits with a backstop at 10 s, which answers the existing timeout reply
   (`detentd.cc:306`, `:340-342`). The entry replaces `pending_commands_`, `pending_mutex_` and
   `pending_cv_` (`detentd.hh:310-312`).
2. **Applies.** `on_raft_apply` (Bus loop) spawns the envelope's apply as a task on the owner's
   scheduler and returns. The apply never suspends and the owner has one thread, so applies run in log
   order. The owner commits it and, when the pending map holds its correlation id, settles that entry.
   Raft keeps its log in memory and does not depend on apply completion (`raft.h:234-237`).
3. **Commit.** A task that never suspends. It resolves against the owner's `current_snapshot_` with no
   lock, which keeps PATCH's merge race-free (`detentd.cc:183-186`), serializes, persists before
   publishing (`:194-204`), diffs and compresses, then publishes.
4. **Publish.** One `atomic_shared_ptr<const Published>` (as at `include/detent/snapshot_ref.hh:19`)
   holds the version, the ETag, the body and its gzip and zstd forms, the last diff and its encodings,
   the diff history with its entries shared by `shared_ptr`, and a `generation_cache::Cache` for the
   composed diffs of that version. `snapshot_mutex_` goes.
5. **Reads.** GET and health load the pointer and never wait on a commit; a GET copies only the
   negotiated encoding (today all three, under the shared lock, `detentd.cc:583-591`). The cache lives
   in the `Published` the GET loaded, so an insert always describes its version.
6. **Measurement (C1).** The owner logs each commit's duration; the lead records 560 KB and 16 MiB in
   S3. Slicing is reconsidered only past a quarter of the shutdown grace (1.25 s).
7. **Lifetime.** `~Distributor` stops the Bus first (`detentd.cc:113-118`), so no apply is spawned after
   it; D15 orders the rest.

**D14. Blocking points assert they are off the loops (L5, N2).** `detent::assert_off_loop()` is
`assert(!reactor::inside_run())`, active without `NDEBUG`. `inside_run` and `RunScope` arrive with
reactor `d381e4d`, after Detent's pin `fe8c5ce` (`cmake/dependencies.json:111-113`), so the helper lands
in S2 with the reactor pin. It guards:

1. The Distributor commit and its entry points (`resolve_and_commit`, `try_accept`,
   `try_accept_patch`).
2. `persist_locked` and `read_file`.
3. Any remaining `std::future` or condition-variable wait in `src/detentd`, `src/detent_authority` and
   `src/detent_relay`.

`OwnerLoop`s run plain `io_context`s, where `inside_run()` is false. The cluster Bus is a
`reactor::UdpServer` (`cluster/bus.h:24,123`), so the assertion fires if a commit runs there again.

**D15. Teardown runs task's four phases across http's reactors and Detent's owners.** Frames cross
schedulers here (a handler's child waits on an owner's event), so one scheduler's order is not enough
(task `cancellation.md` D8; TH2-R1). Each process runs, in order:

1. `begin_shutdown()` (D10), so no owner wait outlives the HTTP stop in normal operation.
2. Phase 1: stop and join every loop: the HTTP service's reactors, then each `OwnerLoop`. An owner whose
   runtime drains on its own loop (the authority) is drained first (`Service::drain()`), and a host with no
   HTTP reactors (the managed host) has one loop, so its phase 1 is `io.run()` returning.
3. Phase 2: `close()` on every reactor scheduler and every owner scheduler.
4. Phase 3: `destroy_roots()` on all of them, with every `io_context` alive.
5. Phase 4: destroy the HTTP service, then the handler and its owners.

`detentd` (`main.cc:241-310`) and the relay host (`host.cc:374-382`, which today stops the listener,
joins the owner, then resets the facade) follow it. The HTTP service must expose its phases as separate
calls: TH2-R1 asks reactor's `TcpServer` to, and Detent needs `HttpAsioService` to forward them
(dependency 2 below). T13 is the zero-live-frames row.

**D16. Detent's other `asio` coroutines move to task, one rule per construct.** The architect ruled them
into W2 (http Q2). Each construct has one replacement from task's API (`docs/API.md`):

| Today | After |
|---|---|
| `asio::awaitable<T>` | `task::task<T>` |
| `co_spawn(executor, t, done)` | `task::spawn(task::scheduler_ref::of(sched), t, done)` |
| `co_spawn(..., use_future)` driven by `io.run()` (`managed_host.cc:169`, `https_client.hh:102`) | `task::spawn` with a done callback, the loop run on the calling thread |
| `op \|\| timer.async_wait(...)` (`client.cc:301-303`, `https_client.hh:82-84`) | A `task::deadline` whose token every operation inside takes through `await_asio` (task D4: a deadline is a token source, not `when_any`); the expiry maps to today's exception (`SourceUnavailableError("delivery network deadline")`, `client.cc:305`; `"HTTPS health deadline"`, `https_client.hh:86`) |
| `a && b` (`coordinator.cc:1178-1179`, `managed_host.cc:109-111`) | `task::when_all(a, b)` |
| `bind_cancellation_slot(network_cancel.slot(), ...)` (`coordinator.cc:851-916`) and `network_cancel.emit` (`:704`, `:1221`) | A `task::cancel_source` whose token the client functions take as a parameter; `emit` becomes `request()` |
| `reset_cancellation_state(disable_cancellation())` (`completion_scheduler.cc:94`, `coordinator.cc:1174`) | Structural: the operations take no token |
| `co_await this_coro::executor != owner` (`completion_scheduler.cc:89`) | `assert(sched.runs_here())` |
| `co_await asio::post(io, use_awaitable)` | `co_await task::yield()` |
| `timer.async_wait(use_awaitable)` | `await_asio<void(asio::error_code)>(...)`, with a token where something cancels it |
| `experimental::channel`, `cancellation_signal` (`fleet_load.cc:27,47,561`) | An event in the slot's state; a `cancel_source` |

The delivery client's public functions (`client.hh:109-115`, `client_probe_internal.hh:10-14`) become
`task::task<T>(..., task::cancel_token = {})`. Their only consumers are Detent's own: the coordinator,
`fleet_load`, the tests above, and `https_health` from `src/detentd/main.cc` and
`src/detent_relay/main.cc`.

## User-visible changes

| Change | Who sees it |
|---|---|
| Oversize PUT/PATCH and authority bodies get 413 `Payload Too Large` (text/plain, close) instead of a dropped connection; a `Content-Length` over the cap is refused before any body byte | Publishers sending > 16 MiB or > `request_bytes` |
| The old offload 503 is gone; the classic write-slot refusal is 503 + `Retry-After: 1` before the body | Classic publishers past 1024 pending writes |
| Authority reply over budget or size: JSON 503 (`outcome_unknown` for commands, `admission_full` for reads) or 413 `reply_bound` instead of a close (D9) | Publisher readers and writers |
| Authority `GET /v1/health` with a body: 400 JSON `health_body_forbidden` instead of a dropped connection | Non-conforming clients |
| `CONNECT` answered by the existing routing and the connection closed; HTTP/1.0 with `Transfer-Encoding` closes; trailers no longer reach `header()` | Non-conforming clients |
| http-parser's strict mode drops requests with a bare LF, obs-fold, or a non-`chunked` `Transfer-Encoding`, without a reply | Non-conforming clients; Detent's own clients are checked in T7 |
| Shutdown answers waiting writers at once by the outcome contract, buffered (D10); stalled sockets are forced after 5 s | Clients during shutdown |
| `--workers`, `DETENTD_WORKERS` and the config field `workers` are gone (Q2) | Operators, `docs/USAGE.md:160`, the website CLI page, harnesses |

Not changed: the receive window and idle keep-alive (both T, D8); a publisher reply keeps the
connection alive when the request did (N3); a half-closed client still gets its outcome (D10); the
delivery client's wire behavior and exceptions (D16).

## Limits

- A handler that gains heavy synchronous work stalls its loop. D14 catches it only at asserting
  points, and T3's lag probe only on exercised paths.
- D4 relies on one request per connection; T4 pins it.
- The authority parses up to `request_bytes` on the loop: an estimated 50-150 ms per maximum request
  in a 16 MiB configuration (S4 times it).
- The composed-diff GET stays on the loop: an estimated 0.3-0.8 s at 16 MiB, once per base version per
  publish (Q5).
- A 16 MiB classic commit delays the next writer by an estimated 0.4-1.2 s (writes are serial today,
  `detentd.cc:181`).
- The owner's apply backlog holds envelopes Raft committed and the owner has not applied; Raft holds
  the same entries, so the memory at most doubles, bounded by the write slots (D3).
- A stop path that skips `begin_shutdown()` leaves its writers waiting until the backstop (T + 1 s for
  the authority, 10 s classic). T6 and T13 cover both binaries; a new embedding must call it.
- A handler frame over 2 KiB is allocated per request (task-handlers Limit 1); `task::sync_wait` on a
  loop thread deadlocks it (Limit 2).

## Designs this depends on or changes

1. **http `docs/design/task-handlers.md` revision 2** (`http:209ceef`): D1 signatures, D2 child
   handler, D4 deadlines, D5 token-only request cancellation, D7 writes, D9 affinity, R2
   `reactor(i).scheduler()`. W2's handler steps start once H1 lands, with task C1-C3 and reactor R
   before it, and http-log L for the classic `AccessLog` (`main.cc:269-275`).
2. **TH2-R1** (`task-handlers-rev2-design.md`): D15 needs `HttpAsioService` to expose stop, close and
   destroy-roots as separate calls that forward reactor's. Without them D15 cannot run phase 3 after
   the owners join. This is a request to the http designer, not a Detent step.
3. **task `docs/design/cancellation.md`** (D1-D5, D8) and `docs/API.md` at `task:cbfee34`: tokens,
   callbacks, deadlines, `await_asio` with a token, `close()` and `destroy_roots()`.
4. **PLAN W2** (`PLAN.md:51`) still says `asio::awaitable<void>`; the lead edits the row
   (task-handlers names the same edit). PLAN criterion 3 is unchanged.
5. **[HTTP_LOGGING_ARCHITECTURE](../HTTP_LOGGING_ARCHITECTURE.md):** the offload 503 and deferred
   replies it covers go away; classic replies stay buffered.
6. **[ASYNC_AUTHORITY_ARCHITECTURE](../ASYNC_AUTHORITY_ARCHITECTURE.md):150** and
   **[AUTHORITY_TRANSPORT](../AUTHORITY_TRANSPORT.md):55:** the deferred reply becomes D2's hop and
   D8's owned stream; the contract (outcome deadline plus 1 s to write) holds.
7. **[RELAY_ARCHITECTURE](../RELAY_ARCHITECTURE.md):162:** "HTTP intake workers" become the loop.
8. **[src/detentd/CLUSTERING.md](../../src/detentd/CLUSTERING.md):** applies run on the Distributor
   owner, in log order (D13).
9. **[SHARED_LIBRARY_DECISIONS](../SHARED_LIBRARY_DECISIONS.md#coroutine-drivers-for-journal-and-consensus-november-2):**
   journal and consensus drivers stay M1b; the completion driver is Q6.
10. **W3:** its reactor, http-parser, http, http-log, cluster and consensus pins land with S2.

## Gate rows

Builds and suites run under `quiet light` with at most 4 jobs. Configure with
`-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON`. The sanitizer builds are separate:
`build-asan` (`-fsanitize=address,undefined`) and `build-tsan` (`-fsanitize=thread`), Homebrew LLVM,
without `NDEBUG`. Rows that count frames build their tests with `TASK_COUNT_FRAMES`.

| Row | Target | Procedure |
|---|---|---|
| T1 Owner wait | (b) first (N6): a stop request, made before the child reaches the owner and while it waits, wakes the wait and returns the truthful reply within 50 ms; (a) a completion settled before and after the child starts waiting; (c) a completion after the wait ended is clean; (d) a backstop expiry answers the backstop reply; (e) a hop after an authority runtime failure answers 503 `stopping`; (f) mutant: without the `cancel_callback`, (b) times out | `ctest -R owner_wait` in native, `build-asan`, `build-tsan`; (f) on a scratch commit, reverted |
| T2 Writes never block reads | `--reactors 1`. Unclustered: a test hook holds the owner mid-commit on a latch; `GET /v1/snapshot` and `GET /v1/health` answer with the old version before the latch opens, then the new one. Clustered without a quorum: a waiting PUT, then `GET /v1/health` on the same loop answers before the PUT's 503 | `tests/detentd/test_distributor_basic.cc`, `test_raft_cluster.cc` |
| T3 No loop blocking | (a) D14 at every blocking point across the suites; (b) with `enable_lag_probe()`, T2's and T6's fixtures keep `reactor(i).lag().snapshot().max_us` under 250,000 while a 10 s wait is pending; (c) mutant: the classic commit moved back into the handler fails (a) and (b) | Fixtures assert the lag; (c) on a scratch commit |
| T4 Admission | Each Detent cap (authority requests, per producer, bytes, subscription lanes, relay lanes, classic write slots) refuses with Detent's bytes before any body byte; every account returns to zero; `reserved_reply_bytes` returns to baseline after a late completion; a TLS client that does not read keeps `reserved_reply_bytes` until its write ends; no request sees http's gate (1025 concurrent clients against the authority) | `test_service.cc`, `relay_network_fixture` cases, native, `build-asan`, `build-tsan` |
| T5 Body caps | Each D5 row: cap accepted, cap + 1 refused, with `Content-Length` and chunked; the 20 MiB classic PUT gets 413, or a reset if the client's write fails first | `test_distributor_basic.cc:112` rewritten; authority and relay cases |
| T6 Shutdown and disconnect | `begin_shutdown()` then `stop()` with waiting writers: not queued 503 `stopping`, queued 503 `outcome_unknown`, clustered classic the 503 retry reply, an unclustered commit in progress its real result, 1024 classic writes behind a held commit all answered within the grace, every reply delivered buffered on plain TCP and TLS (N1); disconnect, then `stop()`: `stop()` returns within the grace for both binaries; disconnect while waiting: the handler waits, accounts reach zero, `reserved_reply_bytes` returns to baseline; a half-closed client gets its outcome; over-budget command `outcome_unknown`, read `admission_full` | Authority and classic fixtures, native, `build-asan`, `build-tsan` |
| T7 Wire behavior | CONNECT, HTTP/1.0 with `Transfer-Encoding`, bare LF, oversize, authority health with a body: the user-visible replies; requests from the delivery client and `https_client` replay through strict http-parser without rejection | New `test_http_conformance.cc` |
| T8 Suites | Full native suite, Python and Rust bindings, `build-asan`, `build-tsan`, real-process fixtures (`tests/detent_system`, `test_raft_cluster`, `relay_network_fixture`, `test_multivoter.py`); hosted CI green on every job | `ctest -j4` per build; `pytest tests/detent_system` |
| T9 Hot-path A/B (lead) | No regression beyond the A/A spread for `detentd_fanout_bench` GET, `read_response_bench`, classic PUT, authority publish, `detent_read_bench` and the managed first read (sub-millisecond held, PLAN criterion 6); server CPU per request reported beside latency | `cpp-libraries/tools/ab_compare.py` with A = `Detent:6a31280`, A' = a second build of A, B = `main` after W2 lands, `--aa A'`, under `quiet strict` holds of at most 10 min; run by the lead on `main` only |
| T10 Removal | `git grep -nE 'should_offload\|DeferredReply\|DeferredOptions\|->defer\(\|\.defer\(\|user_data\|BodyReader' -- src include tests benchmarks` and `git grep -nE -- '--workers\|DETENTD_WORKERS' -- src/detentd website docs/USAGE.md` (N5) are empty | CI step |
| T11 Deadline timeline | Authority, T = 1 s, plain TCP and TLS: a body trickled to just under the receive bound, an outcome held to its deadline, the whole 503 `outcome_unknown` arrives; an 8 MiB read reply arrives whole; a second request on the same connection is answered (N3); an idle keep-alive connection closes after T | Fixture on `.scratch/arch-review/d8_deadline_probe.cc`'s shape, native and `build-tsan` |
| T12 One shape | `git grep -nE 'asio::awaitable\|use_awaitable\|awaitable_operators\|co_spawn\|this_coro\|experimental::channel' -- src include tests benchmarks` is empty (the architect's condition on http Q2, `benchmarks/fleet_load.cc` included) | CI step, added in S9 |
| T13 Teardown | After D15 with a write suspended on an owner and a forced stop (grace 0): `task::detail::live_frames()` back to its baseline, `detail::orphaned_waiters()` 0, ASan clean, for `detentd` classic, the authority and the relay host | `ctest -R teardown` in `build-asan` |

## Owner questions

Q1-Q4 are answered as the architect recommended (owner direction, 2026-11-02): one Distributor owner
with an unsliced commit (D13); `--workers` removed with an error (D11); http's gate unreachable and
Detent's JSON admission the cap (D4); `user_data` and `defer()` leave http after W2, with `xapiand-74`.

**Q5 (lead). The composed-diff GET** (D6) compresses up to snapshot size on the loop, an estimated
0.3-0.8 s at 16 MiB, once per base version per publish. Options:

1. Keep it on the loop and measure it in M3.
2. The owner precomputes every base's composed diff at each publish, up to 64 per commit.
3. A cache miss hops to the owner and can wait behind a commit.

*Recommendation: 1. It is inherited and cached; the others multiply the commit's work or put GETs
behind commits.*

**Q6 (owner). The completion driver's `asio::awaitable` API.** `AsioCompletionDriver::run` and
`run_one` return `asio::awaitable<void>` (`completion/include/completion/asio_completion.h:101-112`),
and Detent awaits them at three sites (`service.cc:1111`, `completion_scheduler.cc:122`,
`coordinator.cc:674`). Its other users are completion's own `test/asio_extension_test.cc` and
consensus's `test/completion_asio_test.cc` and `bench/worker_completion_bench.cc`; journal, cluster,
strata, prism and Xapiand do not use it. Options:

1. completion's driver returns `task::task<void>` in this wave (`await_asio` with a token over its
   notification descriptor, `task::yield()` for its 16-step turn), Detent owning the change and
   migrating completion's and consensus's test and bench, coordinated with `xapiand-74` (PLAN rule 4).
2. Leave it for M1b, and let T12 carry a named exception for the three sites until then.
3. A Detent wrapper that awaits the asio driver from a task.

*Recommendation: 1. Option 2 leaves Detent on two coroutine types until M1b, the mixed shape the owner
ruled out; option 3 is a bridge, which the one-shape rule forbids. The driver is about 100 lines with
no consumer outside Kronuz, and M1b's journal and consensus drivers then start from task.* It blocks
S7 and S8 only.

## Follow-ups (not W2 steps)

1. The authority runtime takes HTTP tasks from `queue_` under `mutex_` on a 1 ms tick
   (`service.cc:2370-2387`, `:1612-1628`, `:2527-2531`). The child already runs on the runtime, so it
   can enqueue there and wake the runtime directly.
2. The relay owner's `turn` re-arms on a 1 ms timer (`relay/host.cc:340-346`).
3. In http: an additive `Request::deadline()`; removing `user_data` and `DeferredReply` (Q4).
4. Persist through completion's async file operations on the Distributor owner.
5. M1b: journal and consensus drivers on task, then Detent's owner loops on them.

## First steps

Every commit on the branch builds and passes the suites it touches. S1 and S6 need only task: they
start now against task C2 (`task:cbfee34`) and land after task C3 lands. S2 starts as soon as H1
lands; the others follow their inputs.

| Step | Content | Starts when | Done when |
|---|---|---|---|
| S1 Owner wait | The task pin in `cmake/dependencies.json` (`task::asio`); `detent::OwnerLoop`; a unit test of D2's shape with a plain loop scheduler standing in for a reactor: T1(b) written and passing first (N6), then (a), (c), (d), and (f)'s mutant | Now, against task C2 | T1(a)-(d) pass in native, `build-asan`, `build-tsan`; (f) fails with the callback removed, shown once in the ledger; `live_frames()` back to baseline |
| S2 Pins and mechanical conversion | Pins: task, reactor, http-parser, http, http-log, cluster, and consensus where W1 landed; `HttpAsioService` and `ConnectionFactory` (`main.cc:244,278,284`, `host.cc:243-245`) with D4's one-value caps; the three handlers and the classic routes as `task::task<void>`, still blocking inside (S3 and S4 remove it); `run_handler` and the ~30 test sites (D12); the relay migration (D3, D5); `assert_off_loop` (D14, N2); T7 | H1, R, task C3 and http-log L landed; W1 for consensus | Full native suite green; T7 passes; T4's relay rows pass; T10's first pattern has no relay hit |
| S3 Classic | Distributor `OwnerLoop`, `Published` (D13), hops for PUT/PATCH, applies spawned on the owner, write slots, `set_max_body`, `begin_shutdown()` and D15 in `main.cc`, D14's calls; the commit-duration log (C1) | S2 | T2, T5, T6 classic rows and T13 classic pass in native, `build-asan`, `build-tsan`; the lead's 560 KB and 16 MiB commit times recorded |
| S4 Authority | Runtime `OwnerLoop` as a `Service` member (D2 lifetime 2, N4); `Runtime::run` and its waits on task (`service.cc:1137-1161`, `:1612-1628`, `:2527-2737`; the driver await at `:1111` waits for S7); `create_extension`, the hop, owned-stream replies with `request.keep_alive` (D8, N3), buffered shutdown replies (N1), the reply budget (D9), `begin_shutdown()` (D10); tests off `DeferredWriter`; the parse timing (C2) | S2 | T1(e), T4, T6 authority rows, T11 and T13 authority pass in native, `build-asan`, `build-tsan`; T10's first pattern empty |
| S5 `--workers` | D11 and its docs | S2 | T10 empty, both patterns |
| S6 Delivery and HTTPS clients | D16 for `https_client.hh`, `client.hh`, `client.cc`, `client_probe_internal.hh`; their tests (`test_client.cc`, `test_probe_client.cc`, `test_reconnect_cancel.cc`, `bootstrap_tls_fixture.hh`, `test_bootstrap_retention.cc`, `test_transport_https.cc:120`, the client half of `relay_network_fixture.hh`) | S1 (the task pin) | Those files have no T12 match; their suites pass in all three builds; a test expires the network deadline mid-body and gets `SourceUnavailableError("delivery network deadline")`, and fails with the deadline's token not passed |
| S7 Completion driver | Q6 option 1: completion's driver on task, completion's and consensus's test and bench migrated; then Detent's `CompletionScheduler` (`completion_scheduler.hh`, `.cc`), `service.cc:1111`, `test_completion_scheduler.cc`, `test_transport_peers.cc` | Q6 answered; S1 | completion and consensus CI green on every job; Detent's touched files have no T12 match; `test_completion_scheduler` passes in all three builds |
| S8 Coordinator, managed host, relay owner | D16 for `coordinator.hh`, `coordinator.cc`, `managed_host.cc`, `managed_host_internal.hh`, and `host.cc:362` on a relay `OwnerLoop` with D15; their tests (`test_coordinator.cc`, `test_preparation_deadline.cc`, `test_prepared_startup.cc`, `test_worker.cc`, `test_distribution_service.cc:243`, the coordinator half of `relay_network_fixture.hh`) | S6, S7 | No T12 match outside `benchmarks`; those suites pass in all three builds; T13 relay passes |
| S9 Benchmark and the grep | `benchmarks/fleet_load.cc` on task; T12 as a CI step | S8 | T12 empty; `fleet_load` runs its smoke configuration |
| S10 Proof | T3 with its mutant, T8 in both sanitizer builds; then the lead's T9 | S3-S9 | T3 and T8 pass; the lead records T9 |
