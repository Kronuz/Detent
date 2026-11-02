# Detent review: W2 HTTP coroutines, preliminary design

- Date: 2026-11-02.
- Reviewed: `docs/design/http-coroutines.md` at `Detent:5c58c31` (branch `design/http-coroutines`),
  ledger `docs/status/http-coroutines.md`; Detent callers at `Detent:5c58c31` (`src/detentd/`,
  `src/detent_authority/service.{cc,hh}`, `src/detent_relay/{service,host}.cc`,
  `src/detent_distribution_http/reply_pump.cc`); http `679c0f8` (`http_asio.h`, `http_handler.h`,
  `http_message.h`, `http_request_parser.h`, `docs/mechanisms/coroutine-handlers.md`), reactor
  `ade618d` (`reactor.h`), http-log `431b878`, task `8cad661`, cluster `428d57d`, Asio at Detent's pin
  (`231cb29`, `asio/impl/co_spawn.hpp`), `cpp-libraries:5eedcc7` `docs/UPGRADING-2026-10.md`.
- Checked against: PLAN.md M1 (W2 row, done criteria 3, 4, 6), rules 1-3; the owner direction of
  2026-11-02 (Detent moves to an async model; prefer Asio's mechanisms; Q1 as an owner coroutine;
  inventory of blocking waits); `docs/reviews/calibration.md` (configured bounds reach consumers;
  holders that export member addresses).
- Checked by: source reading at the commits above; probes in `.scratch/arch-review/`:
  - `d8_deadline_probe.{cc,sh,out}`: a coroutine handler on http `679c0f8` and reactor `ade618d`
    headers with D8's timeouts (T = 1 s), a body trickled within every per-read bound, an outcome
    wait of T, and replies of 139 bytes and 8 MiB. It runs on plain TCP and links the http-parser
    and compressors archives of `http-wt/l3-http/build-l3` (A1).
  - `t3_t10_grep_probe.sh` and `.out`: the T3(a) and T10 patterns run over today's tree (A4, A9).
  - `blocking_inventory.out`: the waits in the files the owner named (A11).
  - `http-head/`, `reactor-head/`, `http/`: exported head sources the citations use.
  - Not run: benchmarks, fuzzing, Detent builds and suites (the design has no code yet); no
    measurement host touched.

## Verdict

**REJECT: the direction holds, but D2, D8, D9/D10, the classic lock and T3 must change before S1
starts.**

What holds, and reproduces:

- Premise correction: `Request::user_data` and `defer()` still exist at http head
  (`http_message.h:126`, `http_handler.h:105`).
- D3: `create_extension` runs at headers-complete, after the gate and before any body byte or the
  413 check (`http_asio.h:751-763`, `http_request_parser.h:242-255`). Its reservations release on
  every path. On 413 and on a disconnect mid-body, the parser is a local of the `try` block
  (`http_asio.h:745`), so unwinding destroys its `Request` and extension before the catch at
  `:970`. On success, cancel and shutdown, the `Request` is a loop-iteration local destroyed after
  `respond()` (`:940`, `:965`).
- D5: accepted sizes do not change. The old sink refuses `size > 16 MiB` (`detentd.cc:152`), and
  http refuses `Content-Length > cap` (`http_request_parser.h:255`) and a chunked body past the cap
  (`:268`).
- D4 holds for TLS and for owned streams (review ask 2). A gate lease spans headers-complete to the
  settled response (`http_asio.h:747,752,921,966`), so leases never exceed sessions. The
  per-reactor limit (`:1014`) equals the process connection cap (`main.cc:278`; relay
  `host.cc:243-245`).
- D6: distribution encoding never ran on HTTP workers or loops. The pump publishes buffers that the
  runtime already encoded (`reply_pump.cc:40-46`).
- http-log forwards `create_extension`, `on_request_body` and `wants_body_stream` to the classic
  handler (`http_log.h:440-447`).
- Q4's evidence holds. No `user_data`, `defer(` or `DeferredReply` appears in Xapiand `src/` at
  `Xapiand:8ef0f8633`, strata `fe6d3ca`, prism `da169a9` or http-log `431b878`.

What blocks: L1 (A5), L2 (A1), L3 (A2), L4 (A3), L5 (A4).

## Findings

**A1. D8 can cut the reply to a submitted command, and it widens the receive window from T to
2T + 1 s (blocking).** At head, one `request_deadline` is computed at the top of the keep-alive
loop (`http_asio.h:815-817`). It bounds the header and body reads (`:825`, `:931`). It drops the
request with no reply when the body ends after it (`:939`). It also bounds the buffered reply's
write (`:794`, `:965`, `write_with_progress` `:389`). The handler's outcome wait is not part of that
budget.

With D8's settings (request 2T + 1 s, header T, inactivity T):

1. The body can take up to 2T + 1 s while every read stays under T.
2. The handler then waits up to T for the runtime's outcome.
3. Write budget left = 2T + 1 s − receive − T. This is zero or negative once receive exceeds T + 1 s.
4. The command was queued (`service.cc:799`), so it may commit. Whatever of the reply has not left
   by the deadline is cut, and the connection closes.

On plain TCP, a reply small enough to leave in one non-blocking send escapes the cut: `send_now`
writes it before any deadline check (`http_asio.h:804-807,963`). Every other reply depends on the
remaining budget:

1. A reply larger than the socket buffer, such as a read reply up to `reply_bytes` (16 MiB).
2. Every TLS reply. TLS has no `send_now`, and non-loopback authority binds require TLS.

Today the receive window is T (`main.cc:279`). The deferred reply has its own outcome + 1 s write
deadline (`service.cc:967`, `AUTHORITY_TRANSPORT.md:55`), so the 503 always has 1 s. The 2T + 1 s
window also lets a trickling client hold an admission slot about twice as long. The comment at
`http_asio.h:94-95` says this deadline exists to prevent that. The longer `request_timeout` also
bounds the idle keep-alive wait (`:825`), so idle connections live 2T + 1 s instead of T.
Probe: `d8_deadline_probe.sh`; result in A1-result below.

Review ask 3, TLS replies after the outcome deadline: not correct as designed. TLS has no
`send_now` fast path, so every TLS reply goes through `write_with_progress` under the same deadline.

Fix: state a per-phase timeline in which a submitted command always has at least 1 s to write its
reply, and keep the receive window at T unless the change is listed as user-visible. One way needs
no library change. The extension records its headers-complete time. `handle` refuses to submit
when the body took longer than T (a 503 before submission is truthful). Set
`request_timeout = T (headers) + T (body) + T + 1 s (outcome) + 1 s (write)`. Add a gate row for
this case: a body trickled to just under the receive bound, then an outcome held to its deadline.
The client must receive `outcome_unknown`.

A1-result (`d8_deadline_probe.out`, T = 1 s, deadline 3 s, body trickled over 2.4 s, outcome after
1 s):

| Case | Reply received | When |
|---|---|---|
| Fast body, 139-byte reply | 139 of 139 bytes | 1001 ms |
| Trickled body, 139-byte reply | 139 of 139 bytes; `send_now` escapes the deadline | 3434 ms |
| Fast body, 8 MiB reply | 8,388,708 of 8,388,708 bytes | 1312 ms |
| Trickled body, 8 MiB reply | 327,212 of 8,388,708 bytes, then close | 3437 ms |

A TLS case was not run. It takes the async path for every reply, so it is the 8 MiB case
regardless of size.

**A2. The replies to an admitted command break the outcome contract (blocking).**
`AUTHORITY_ARCHITECTURE.md:81` requires `outcome_unknown` whenever an admitted command's outcome
is unknown, and the runtime follows it: queued publisher tasks get `outcome_unknown` at shutdown
(`service.cc:2706-2712`) and at expiry (`:1780-1787`). The design breaks it in three places:

1. D10 answers a shutdown-cancelled wait with `503 {"error":"stopping"}`. The command may already
   be in consensus. `stopping` is reserved for session tasks (`service.cc:2554,2564,2694`) and for
   commands refused before they are queued (`:793`).
2. D9 answers an over-budget reply with `503 admission_full`. In this contract that means "not
   admitted", but the command has executed. A same-identity retry returns the retained outcome, so
   `outcome_unknown` is the truthful reply. For reads (`/v2/read`, `/v2/result`), `admission_full`
   is acceptable.
3. On the classic path, a shutdown 503 can reach a PUT whose commit has been posted to the commit
   owner (Q1) or added to the Raft log, and that commit still lands. Use the existing timeout reply
   (`detentd.cc:340-342`, Transport, "retry"), or let the owner finish a commit that has started
   (its time fits inside the 5 s grace).

D10's Disconnect rule ("the handler returns, and its reply is attempted") does not say which reply.
It also changes half-close behavior. The deferred-reply monitor treated EOF as not a disconnect.
The in-flight watch treats EOF as one, and on plain TCP the first `on_cancel()` peeks and cancels
at once (`coroutine-handlers.md`, "Cancel reads, not writes"; `http_message.h:144-148`). Today, a
client that half-closes after sending gets its outcome. Under D10 it would not. Fix: on Disconnect,
keep waiting, since the wait is bounded by T. Answer early only on Shutdown.

**A3. Classic GET and health block their loop on the commit's lock in both modes, whatever Q1
decides (blocking).** `route_get_snapshot` and `route_get_health` take
`std::shared_lock(snapshot_mutex_)` on the loop (`detentd.cc:488,544,625`). `resolve_and_commit`
holds the exclusive lock through the following, an estimated 0.4-1.2 s at 16 MiB:

1. Resolve.
2. Serialize.
3. File write and rename (`persist_locked`, `:414-424`).
4. Diff.
5. Four compressions (`:181-239`).

Clustered, the commit runs in `on_raft_apply` on the Bus loop. The Bus is a `reactor::UdpServer`
(`cluster/bus.h:24,123`), so a reactor loop does that work, and every HTTP loop that serves a GET
during it waits. The design puts the off-lock precompute only inside Q1's option 1 recommendation.
T3(a) cannot see a lock wait.

Fix: make it a decision for both modes. A single owner (A6) computes the new state without the
lock, then publishes it in O(1). The publish is either a microsecond swap under the lock or an
`atomic_shared_ptr` of immutable state. Detent already pins `atomic_shared_ptr`, and that would
also remove the 16 MiB × 3 copies a GET makes under the lock (`:583-591`). Move clustered applies
to the same owner: `on_raft_apply` posts the envelope there in log order.

**A4. T3 cannot be run as written and does not test criterion 3 (blocking).**

1. T3(a)'s pattern matches the design's own primitive (`wait_until`) and runtime code in the
   handler files, such as the startup `future.get()` at `service.cc:570`.
2. It cannot see lock waits (A3) or blocking reached through helpers. Probe:
   `t3_t10_grep_probe.out`.

Reactor has the precise signal: `reactor::inside_run()` is true only on loop threads
(`reactor.h:135-142,193`). The authority runtime and the relay owner run plain `io_context`s, so it
is false there.

Fix: add one helper, `detent::assert_off_loop()` (`assert(!reactor::inside_run())` in tests and
debug builds). Call it in every blocking point Detent owns: the snapshot publish lock, persist,
`read_file`, any remaining `std::future` or condition variable, and the commit owner's entry
points. Keep T3(b)'s lag probe, and add a mutant that fails it: move the classic commit back
inline. Make T2's unclustered case deterministic: a test hook holds the commit mid-precompute,
then the GET must answer.

**A5. Owner direction on D2: replace `Outcome<T>` with Asio's own hop. No new Detent type is needed
(blocking until the design is revised).**

Where a handler starts work, use
`co_await asio::co_spawn(owner_executor, work(), asio::bind_cancellation_slot(signal.slot(), asio::use_awaitable))`.
This is enough, together with a one-shot local to the owner for completions that arrive later.
Each later completion is first posted to the owner, so that one-shot is single-threaded. It can
be `asio::experimental::channel<void(asio::error_code, Reply)>` (capacity 1) on the owner executor,
raced against a `steady_timer` with the awaitable operators:

1. Classic: `on_raft_apply` posts to the Distributor owner (A3, A6). The owner keeps the pending
   map and settles the channel.
2. Authority: `complete` already runs once, on the runtime thread (`service.cc:1333-1365`;
   `task->settled`).

What the hop gives, at Asio `231cb29`:

1. It resumes on the right executor. On completion, co_spawn posts to the owner, then dispatches to
   the caller's executor through `handler_work` (`co_spawn.hpp:156-159,181-184,215-218`).
2. Exceptions from the work rethrow in the handler.
3. Cancellation crosses threads safely. `co_spawn_cancellation_handler` dispatches the emit to the
   owner executor (`co_spawn.hpp:260-264`). From a loop thread that is a post, so the wait never
   resumes inside `Request::cancel`. With `Outcome::abandon()`, an inline resume could destroy the
   `Request` whose `cancel()` is still on the stack: the plain-TCP `send_now` path can reach the end
   of the loop iteration with no suspension (`http_asio.h:806-807,963-969`).
4. One completion: the coroutine completes once.

What the design must state:

1. Bind the slot explicitly. http disables the connection coroutine's cancellation around `handle`
   (`http_asio.h:953`). The handler bridges `Request::on_cancel` (on the loop, Shutdown only per
   A2) to `signal.emit(terminal)`.
2. Cancellation must end the owner-side wait with the truthful reply (A2), never by dropping the
   command.
3. Lifetimes. The loop's `io_context` must outlive the spawned work: `handler_work` tracks work on
   it. `TcpServer::stop()` waits for sessions before stopping the loops (`reactor.h:595-597`), and
   D12's `run_handler` must keep that order. The owner's `io_context` must run until the HTTP
   service has stopped. That holds when the service is destroyed before the owner (`main.cc:241`
   vs `:278`). Each owner-side channel must be destroyed before its owner's `io_context`; check
   that the runtime's remaining tasks die before `runtime.io` does.
4. Cost: one frame and about four cross-thread posts per write request (GET does not hop). T9
   covers it.

D2's alternatives section then names `task::event` (`task.hh:687-737`) as the other existing
one-shot and says why it does not fit: it carries no value, has no deadline, and needs a task
scheduler on each loop. T1 becomes bridge rows:

1. Completion before and after the hop.
2. A Shutdown cancel reaches the owner and the truthful reply is written within the grace.
3. A late owner completion after the loop-side wait has ended is clean under ASan and TSan.
4. The channel is destroyed before the owner's `io_context` (ASan).
5. Mutant: dropping the explicit slot binding makes T6's shutdown case exceed the grace.

**A6. Q1 under the async model: a Distributor owner `io_context` with the commit as a coroutine,
not sliced.** The owner serves only snapshot writers (unclustered PUT and PATCH, clustered applies)
and their pending map. GETs read the published state (A3) and never run on it. Writes are serial
today (`detentd.cc:181`), so a monolithic 0.4-1.2 s commit delays only:

1. The next writer, which would wait anyway.
2. Its timers.
3. Delivery of a shutdown cancel, still well inside the 5 s grace.

Slicing gzip, zstd and serialize into yielding steps would need the streaming compressor
interfaces in place of `http::encode` (`detentd.cc:384-393`), and nothing latency-sensitive shares
the loop to benefit. The owner loop absorbs the commit.

The commit also leaves the Bus loop, which today runs it alongside Raft heartbeats.

Conditions:

1. S3 measures the 560 KB and 16 MiB commit once and records it, replacing the estimate.
2. Slicing is reconsidered only if the commit exceeds a quarter of the shutdown grace, or if
   latency-sensitive work moves onto the owner.
3. The owner does blocking file I/O (`persist_locked`); that is acceptable on an owner loop.

This is not an offload pool. It is one serial owner of the snapshot state, the same pattern as the
authority runtime (`service.cc:568,1142`) and the relay owner (`host.cc:358-373`), and handlers
never run on it.

**A7. D6's inventory is incomplete.**

1. The composed-diff GET (`detentd.cc:569-608`) serializes and compresses, gzip and zstd, on the
   loop. "Bounded by 64 history entries" bounds steps, not bytes. A composed diff can approach the
   snapshot size: at D6's own rates, 16 MiB of gzip takes about 0.3-0.8 s on one loop. This is
   inherited, not introduced. Record it in Limits with the estimate, and let the lead decide
   whether it moves to the owner, or whether the owner precomputes the composed diffs it
   publishes.
2. The relay request parse (`relay/service.cc:422`, lane body limit) moves onto the loop and is
   missing from the table.
3. The authority parse on the loop is an estimate. S4 times it at 256 KiB and 16 MiB once, then
   records the result.

**A8. Steps S1-S6 are in the wrong order.** At head, `handle` is pure virtual and returns an
awaitable (`http_handler.h:183`), and the `should_offload` overrides
(`detentd.cc:125`, `service.hh:56`, `relay/service.hh:41`) stop compiling. S2's pin therefore
cannot build with only the relay migrated, and S3 and S4 depend on S2, not only on Q1 and Q3.

Order:

1. S1: the bridge (A5) and its T1 rows, plus the `assert_off_loop` helper (A4), on the current pins.
2. S2: the pins, a mechanical coroutine conversion of all three handlers (still blocking inside),
   `run_handler`, the relay migration, and T7, because http-parser's strict mode arrives with the
   pin.
3. S3: classic.
4. S4: authority.
5. S5: `--workers` and T10.
6. S6: the suites, then T9.

**A9. Gate rows that do not reproduce or do not cover their criterion.**

1. T10's pattern matches 160 lines, 78 of them benchmark fleet `--workers` options
   (`t3_t10_grep_probe.out`), while T8 requires "T10's grep is empty". Use precise patterns:
   `should_offload|DeferredReply|DeferredOptions|->defer\(|\.defer\(|user_data|BodyReader` over
   `src include tests benchmarks`, and `--workers|DETENTD_WORKERS` over `src/detentd docs website`.
2. T9 omits criterion 6's managed first read (sub-millisecond target held). Name it, or point to
   W5's row.
3. T4 and T6 assert "accounts reach zero". After a late completion they must also assert
   `reserved_reply_bytes` returns to its baseline without waiting for the task to retire.
   `ReplyAdmission` releases on destruction (`service.cc:500-511`).

**A10. Accuracy and user-visible gaps (non-blocking).**

1. D10 says `listener` is destroyed before `facade` in `host.cc`. `facade.reset()` runs at `:379`,
   and `listener` lives to the end of the scope. The safe fact is that `listener.stop()` at `:375`
   comes first. State that.
2. The authority's health route with a body changes from a dropped connection (`Sink` capacity 0,
   `service.cc:677`) to a 400 JSON. Add it to the user-visible table, with the idle keep-alive
   change from A1.
3. D4 depends on `in_flight_limit` staying at least the connection cap. Build both from one value
   in `main.cc` and `host.cc`, so the backstop cannot fall below the cap.

**A11. Owner item 3: blocking waits in the named files** (`blocking_inventory.out`).

| Site | What | Class |
|---|---|---|
| `detentd.cc:325-346`, `:358-364`, `detentd.hh:311` | `pending_cv_.wait_for`, 10 s, on the request path | Request path (W2, S3): owner pending map plus channel (A5) |
| `detentd.cc:488,544,625` vs `:181` | GET and health `shared_lock` behind the commit's exclusive lock | Request path (W2, S3): A3 |
| `detentd.cc:181-239` via `:353` | Clustered commit on the Bus reactor loop | Request path (W2, S3): post to the owner (A3, A6) |
| `service.cc:1018-1030` | `promise` / `future.wait_until` fallback for synchronous writers | Request path (W2, S4): removed; tests use `run_handler` |
| `service.cc:2370-2387`, `:1612-1628`, `:2527-2531` | Runtime takes HTTP tasks from `queue_` under `mutex_` on a timer tick | Owner-loop internal (later): a co_spawn hop can enqueue on the runtime and wake it |
| `relay/host.cc:340-346` | Owner `turn` re-armed on a 1 ms timer | Owner-loop internal (later) |
| `service.cc:566-572`, `:581-583` | Constructor waits for the runtime to be ready; destructor joins | Legitimate: process start and stop |
| `managed_host.cc:120-129`, `:143-162`, `:188`, `:192`, `:197` | Startup futures on the main thread; `done.get()` after `io.run()`; join; drain | Legitimate: process start and stop |
| `relay/host.cc:44-54` (`on_worker`, `:127`, `:206`), `:251-252`, `:374`, `:377-378`, `:394-395` | Startup on the main thread; stop future; joins; domain drain | Legitimate: process start and stop |
| `retirement.cc:376-383` `RetirementDrain::wait` | Condition-variable drain; refuses on its worker | Legitimate: called only at shutdown (`managed_host.cc:197`, `relay/host.cc:394-395`) |
| `client_source.cc:85,117,125,133` | Refresh `jthread` sleeps on `condition_variable_any` | Legitimate: client-side reader thread, not a loop |

**A12. Library awaitables and follow-ups.** The authority already drives completion's queue
through `AsioCompletionDriver::run` on its runtime (`service.cc:1111`, `asio_completion.h:101-135`).
The consensus transport's awaitables (`peers.h:251-334`) are internal to the peer set that Detent
starts and stops; no request-path site needs them directly. W2 needs no library API change if A1
is fixed with the extension timestamp.

Follow-ups, outside W2:

1. An additive `Request::deadline()` in http, so a handler can bound its outcome wait by the
   remaining write budget.
2. Q4's removals from http, which need `xapiand-74`'s agreement.
3. Persist through completion's async file operations on the Distributor owner.

## Owner questions

1. **Q1:** option 1, as an owner `io_context` with the commit as an unsliced coroutine (A6). The
   commit's publish happens off-lock in both modes (A3); that is a decision, not an option detail.
2. **Q2:** option 1. Options 2 and 3 are a shim and a silent change of meaning.
3. **Q3:** option 1. This departs from PLAN W2's text ("map onto `in_flight_limit` ... `BodyStream`")
   but is consistent with done criterion 3. The owner should record the deviation when ruling.
4. **Q4:** yes, coordinated with `xapiand-74`. No consumer uses `user_data` or `defer()` (evidence
   under "What holds").

## Blocking items

- **L1 (A5):** Replace D2 with the co_spawn hop and owner-local channel. State the cancellation
  binding, the lifetimes and the T1 bridge rows. Drop `Outcome<T>`.
- **L2 (A1):** Give D8 a per-phase timeline with at least 1 s of write budget for every submitted
  command, and keep the receive window at T. Add the trickled-body gate row.
- **L3 (A2):** A command that may commit gets `outcome_unknown` (authority) or the existing
  timeout reply (classic). It never gets `stopping` or `admission_full`. On Disconnect the handler
  keeps waiting.
- **L4 (A3):** Classic GET and health never wait on a commit: the publish is off-lock or atomic in
  both modes, and clustered applies run on the owner.
- **L5 (A4):** Replace T3(a) with `assert_off_loop` at Detent's blocking points, add the
  revert-check mutant, and make T2 deterministic.

## Conditions

- **C1 (A6):** S3 records the measured 560 KB and 16 MiB commit time. Slicing is reconsidered only
  under A6's thresholds.
- **C2 (A7):** D6 lists the composed-diff GET with its estimate and the relay parse, and S4 records
  the authority parse time.
- **C3 (A8):** Steps are reordered as in A8, and every commit on the branch builds.
- **C4 (A9):** Precise T10 patterns; T9 names criterion 6's managed first read; T4 and T6 assert
  `reserved_reply_bytes` returns to baseline.
- **C5 (A10):** The corrections in A10, and the user-visible table includes the health-body,
  half-close and idle keep-alive changes that remain after L2 and L3.
- **C6:** TSan and ASan/UBSan run as separate builds for T1, T4 and T6 (PLAN criterion 4).

Not run: Detent builds and suites (the design has no code yet), benchmarks, fuzzing, and a TLS
variant of the D8 probe. The D8 probe ran on plain TCP only (5 cases, under `quiet light`).
