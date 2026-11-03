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

## Re-check, 2026-11-02

- Re-checked: `docs/design/http-coroutines.md` at `Detent:8bb0e36` (`git diff e7d58f3..8bb0e36`), ledger
  `docs/status/http-coroutines.md`, against the review above (L1-L5, C1-C6).
- Sources: Detent worktree at `Detent:8bb0e36`; http `679c0f8` and reactor `ade618d` exports in
  `.scratch/arch-review/{http-head,reactor-head}/`, plus `git show origin/main:docs/mechanisms/coroutine-handlers.md`
  in the http checkout; Asio `231cb29` (`impl/co_spawn.hpp`); reactor pin `fe8c5ce` and cluster
  `428d57d`, read-only with `git show`.
- Probes, in `.scratch/arch-review/recheck/`, built and run under `quiet light` (one core at a time):
  - `l2_owned_stream_probe.{cc,sh,out}` adapts `d8_deadline_probe` to the revised D8: `request_timeout`
    = T = 1 s, no header or inactivity timeout, a body trickled over 0.8 s, an outcome wait of T, then
    a 134-byte or 8 MiB reply through an owned stream (deadline = ready + 1 s) or through a buffered
    `send`; plain TCP and TLS (`tls_connection_factory`); a slow reader; and `stop()` 300 ms into the wait.
  - `l1_hop_probe.{cc,out}`: Asio-only model of D2's hop, cancellation slot and capacity-1 channel.
- Not run: Detent builds and suites (the design has no code), benchmarks, fuzzing, sanitizers, a TLS
  slow-reader timing finer than "cut before the full body". No test reverts: the branch has no code.
  `l1_hop_probe` did not finish (see L1).

### Verdicts

| ID | Verdict | One line |
|---|---|---|
| L1 | CLOSED | D2 `design/http-coroutines.md:94-170` states all the review asked; the cancel wake-up is unproven by my probe (N6), T1(b) decides it first in S1 |
| L2 | CLOSED | D8 `:245-273`; owned-stream reply survives an expired request deadline on plain and TLS, 8 MiB to a slow reader is still cut; keep-alive wording N3 |
| L3 | PARTIAL | D9 `:275-285` closed; D10 `:290-299` Shutdown replies cannot leave through an owned stream (N1) |
| L4 | CLOSED | D13 `:327-364`, T2 `:445` |
| L5 | CLOSED | D14 `:366-379`, T3 `:446`, T2 hook; the S1 pin gap is under C3 |
| C1 | CLOSED | D13.6 `:356-360`, S3 `:512` |
| C2 | CLOSED | D6 `:229-231`, Q5 `:474-482`, S4 `:513` |
| C3 | PARTIAL | order matches A8; S1 `:510` cannot build `assert_off_loop` on the current reactor pin (N2) |
| C4 | PARTIAL | T10's first pattern is precise; its second can never be empty (N5); T9 and T4/T6 closed |
| C5 | CLOSED | D10 `:309-312`, D4 `:189-203`, user-visible table `:388,:394` |
| C6 | CLOSED | gate header `:437-440`, T1 `:444`, T4 `:447`, T6 `:449` |

### L1: CLOSED (design); cancel wake-up unproven

The design replaces `Outcome<T>` with `co_spawn(owner, work, bind_cancellation_slot(slot, use_awaitable))`
and a capacity-1 channel on the owner (D2, `:94-170`). The four things the review asked it to state are
there: the explicit slot binding with only the Shutdown bridge emitting (`:128-137`), the lifetimes
(`:139-158`), the cost (`:160-161`), and `task::event` as the alternative with its reason (`:169-170`).
T1 (`:444`) has the review's five bridge rows (a)-(f), in native, ASan and TSan.

Cancellation reaches the owner by post. `co_spawn_cancellation_handler` dispatches the emit to the
owner executor (`asio/impl/co_spawn.hpp:244-269`; the dispatching specialisation is chosen when the
handler has an associated executor, which `use_awaitable` inside a coroutine does). `l1_hop_probe.out`:

```
work: waiting on owner=1        (the hopped work parks on the owner thread)
emit from loop=1                (Request::cancel's bridge runs on the loop thread)
emit returned                   (emit() returned at once; the waiter did not resume inside it)
```

The probe then stopped making progress: neither the cancelled wait nor the work's own 5 s timer resumed
within 12 s, and a `sample` showed both loops idle with `main` waiting for the result. I did not find
the cause (a probe bug, or a lost wakeup of `chan.async_receive || timer` under a bound terminal slot).
Variants with only the channel and only the timer were queued behind exclusive holds and did not
complete. So the post to the owner is confirmed from source and the first three lines, and the cancelled
wait's wake-up is not confirmed by my probe (N6).

Channel placement is sound. The channel is constructed on the owner executor and settled only on the
owner thread. For the authority that is the runtime thread, where `complete` already runs once
(`service.cc:1333-1365`, `task->settled`), so no post is needed there. For the classic path the
apply posts to the owner (D13.2). A late completion holds the `shared_ptr` to the channel and sends into
it after the waiter has gone (probe case D).

The newly found authority `io_context` lifetime defect is real and the fix is sound. `Runtime` is a
local of `Service::run` (`service.cc:1059`, `:1141`). Its `io.run()` returns once the runtime coroutine
ends, and the failure callback at `:1143-1146` only sets `stopping_`. The thread then leaves `run`, and
`Runtime` and its `io_context` die while the HTTP service still accepts requests (`main.cc:241` `app`
outlives `service` at `:278`). A hop to that executor after that is a use after free. Moving `io` to a
`Service` member held by a work guard, released in `~Service` after `stopping_` is set and before the
join (`design:147-158`), has two properties worth stating. First, `io.run()` no longer returns on a
failure, so the `Runtime` local, with its timers, node and driver handlers, stays alive until
`~Service`; no handler can fire on a destroyed `Runtime`. Second, a hop that wins the race with the
failure can enqueue a task nobody will take, and the waiter then ends at its outcome timer with
`outcome_unknown`, which is truthful. Dying in reverse declaration order (queue, then `io`) puts every
task and channel before the `io_context`.

Gap (N4, non-blocking): S4 should say that the work guard is released only after `stopping_` is set,
or `io.run()` never returns and `~Service` hangs on `join()`.

### L2: CLOSED

The mechanism. At head, an owned stream's write is bounded by `exchange->deadline()` alone:
`write_owned_response` (`http_asio.h:568-619`) checks `exchange->deadline()` (`:574`) and writes with
`write_before_deadline(socket, buffers, control->exchange->deadline())` (`:606`). `write_response`
takes that branch first (`:624`), and `respond` calls it before any use of `request_deadline`
(`:793-794`). The request deadline reaches the reply only in `write_with_progress(..., request_deadline)`
for buffered replies (`:794`). Nothing between `handle` returning and the write reads it (`:965-969`).
`serve_tls_connection` ends in the same `serve_connection<ssl::stream>` (`http_asio_tls.h:108`), so
TLS runs the same code. The stream's deadline is fixed when `response.stream(options)` runs
(`http_owned_stream.h:36,268`, `start` returns `Closed` once it has passed), so the handler must create
the stream after the outcome, which D8 says: "measured when the reply is ready" (`:257`).

Measured (`l2_owned_stream_probe.out`, T = 1 s = `request_timeout`, body trickled over 0.8 s, outcome
at T, so the request deadline is 0.8 s old when the reply starts):

| Case | Plain TCP | TLS |
|---|---|---|
| buffered 8 MiB reply | 327,212 bytes, then close | 24,576 bytes, then close |
| owned 8 MiB reply, fast reader | 8,388,703 of 8,388,703 | 8,388,703 of 8,388,703 |
| owned 134-byte reply | 134 of 134, at 1814 ms | 134 of 134, at 1816 ms |
| owned 8 MiB reply, reader at 64 KiB per 50 ms | 1,987,276 bytes, then close | 1,040,384 bytes, then close |

The buffered rows reproduce A1. The owned rows show the revised D8: the reply that A1 cut is delivered
in full on both transports after the request deadline has passed, and the slow-reader rows show the
stream's own deadline (ready + 1 s) still cuts an 8 MiB reply to a reader that cannot take it. (A
small buffered TLS reply also got through, a race between a write that completes at once and an expired
timer; the review's "every TLS reply" was too strong for tiny replies, and 8 MiB is the case that matters.)

The receive window stays T: the revision adds no timeout, and `main.cc:279` is the only
`set_request_timeout` in `src/` (the authority's `timeout`; 0 in classic mode). A trickling client holds
a slot at most T, and the idle keep-alive wait does not lengthen. Not tested: a TLS slow reader to the
millisecond.

Wording gap (N3): D8 cites `start(metadata, length)` (`design:261`). At head `start`'s `keep_alive`
defaults to `false` (`http_owned_stream.h:301`), which writes `Connection: close` and closes the
connection after every publisher reply (`:82-83`). The distribution path passes `true`
(`reply_pump.cc:29,33`). The deferred reply it replaces keeps the connection alive. D8 must pass
`request.keep_alive`, and T11 should send a second request on the same connection.

### L3: PARTIAL

Closed parts. D9 (`:275-285`) answers an over-budget command with `outcome_unknown` and a read with
`admission_full`; D10 (`:290-299`) answers a Shutdown wait with `stopping` only before the task is
queued, `outcome_unknown` after, the existing `Transport` retry reply for a clustered classic waiter
(`detentd.cc:32,340-342` confirm the 503 and the "retry" text), and lets a started classic commit finish.
Disconnect keeps waiting (`:300-302`, T6 includes a half-close). The Disconnect rule matches the
monitor rule at head (`coroutine-handlers.md`, "Cancel reads, not writes", rule 2).

Open part (N1, blocking for S4): the Shutdown replies cannot be written through an owned stream, and D8
sends every reply after a submission through one. `stop()` runs `abort_all()` first, and a stream
registered with the reactor closes with `Shutdown` (`coroutine-handlers.md`, "Shutdown" step 1;
`AsioOwnedStreamControl::abort`, `http_owned_stream_asio.h:27`). `Reactor::track` aborts at once when
the walk has already passed (`reactor.h:208-211`). A handler that wakes from a Shutdown cancel and then
calls `response.stream()` gets an exchange that is closed before `start`. The probe shows it, with the
handler waking 1.2 s after `stop()` began:

```
plain  buffered small, shutdown  :  139 reply bytes (first at 1500 ms)
plain  owned small, shutdown     :    0 reply bytes, stream start=2 (StreamSubmit::Closed)
tls    buffered small, shutdown  :  139 reply bytes (first at 1500 ms)
tls    owned small, shutdown     :    0 reply bytes, stream start=2 (StreamSubmit::Closed)
```

So as designed, T1(b) and T6 ("a queued task gets 503 `outcome_unknown`", "a task not yet queued gets
503 `stopping`") would fail at S4, and a client sees a close with no reply, which is the contract
break L3 was raised for. A stream created before `stop()` fares no better: `abort()` closes it with
`Shutdown` before `start`. The contract holds if Shutdown replies take the buffered path: a handler
that wakes from the Shutdown bridge answers with `w.send(503, ...)` (a few dozen bytes), which the
probe delivers on both transports even though the request deadline had passed. D8's rule needs the
exception "except when the wait ended on Shutdown" and T6 should pin it. The decision to hop only
the writes and answer small JSON errors buffered is already D8's rule for refusals.

### L4: CLOSED

D13 (`:327-364`) names one owner `io_context` and thread, publishes one
`atomic_shared_ptr<const Published>` (`:347-350`; `include/detent/snapshot_ref.hh:19` is the existing
use), and removes `snapshot_mutex_`. GET and health load the pointer (`:351-353`) and never reach the
owner. Clustered applies post to the owner "through a strand ... so applies run in log order"
(`:338-342`). That order holds: the Bus is one loop thread posting FIFO, `add_command` is a mutex and a
signal (`cluster/raft.h:178-181` at `cluster:428d57d`, thread-safe as D13.1 says), the commit never
suspends (`:343`), and the owner has one thread. A strand over a one-thread `io_context` is redundant
but harmless.

No stale insert: today `composed_diff_cache_.try_insert` needs a generation guard because the cache is
shared across versions (`detentd.cc:577-581`, `generation_cache`). In D13 the cache is a member of the
`Published` the GET loaded (`:353-355`), so an insert lands in the cache of the version it was composed
against, and the next publish starts an empty one, as the old `clear()` did. Persist-before-publish is
kept (`:345-346`). T2 (`:445`) is deterministic through a latch inside the commit, and has a clustered
no-quorum case.

Small gaps, non-blocking: the cache inside an immutable `Published` still needs its own
synchronisation (the existing `generation_cache::Cache` has it; D13 should name the type), and the diff
history copied into each new `Published` should share its entries by `shared_ptr`, or a commit copies up
to 64 patches. S3's commit-duration log will show it.

### L5: CLOSED

D14 (`:366-379`) defines `assert_off_loop` as `assert(!reactor::inside_run())` and lists the commit and
its entry points, `persist_locked`, `read_file`, and any remaining future or condition-variable wait.
`inside_run()` is true inside `Reactor::run()` and `RunScope` (`reactor.h:135-143,193`), and the
authority runtime and Distributor owner are plain `io_context`s, as the review stated. T3 (`:446`) is
(a) the assertion across the suites, (b) the lag probe, and (c) the mutant that moves the commit back
inline, which the assertion catches on a loop thread. T2 is deterministic. D12's `run_handler` wraps
tests in `RunScope` so the assertion fires in unit tests.

### C1-C6

- **C1 CLOSED.** D13.6 (`:356-360`) logs each commit's duration and keeps the review's threshold
  (a quarter of the 5 s grace, 1.25 s); S3 (`:512`) assigns the measurement.
- **C2 CLOSED.** D6 lists the composed-diff GET with its estimate and the relay parse (`:229,:231`); S4
  times the authority parse (`:513`); Q5 asks the lead (`:474-482`).
- **C3 PARTIAL.** The order is the review's, with S1 as bridge plus helper on the current pins (`:510`).
  The helper cannot be written there: `reactor::inside_run` and `RunScope` arrive with reactor
  `d381e4d`, and Detent's pin is `fe8c5ce` (`cmake/dependencies.json:112`), which has neither (N2).
- **C4 PARTIAL.** T9 now names the managed first read (`:452`). T4 and T6 assert `reserved_reply_bytes`
  returns to baseline (`:447,:449`). T10's first pattern (`should_offload|DeferredReply|...|BodyReader`
  over `src include tests benchmarks`) is precise: its 27 hits today are all removals. The second
  (`--workers|DETENTD_WORKERS` over `src/detentd docs website`) can never be empty: the design (5
  hits), this review (3), the ledger (1) and `docs/FLEET_QUALIFICATION.md:7`, whose `--workers` belongs
  to the fleet runner, all match (N5).
- **C5 CLOSED.** D10 states `listener.stop()` before `facade.reset()` (`:309-312`; `host.cc:375,379`
  agree). D4 builds the cap and `in_flight_limit` from one value (`:189-198`; `main.cc:278` has the two
  literals today). The user-visible table has the authority health-body change (`:388`) and states that
  half-close and idle keep-alive do not change (`:394`).
- **C6 CLOSED.** The gate header (`:437-440`) makes `build-asan` and `build-tsan` separate, and T1, T4
  and T6 run in both.

### The unclosed limit

`Request::cancel` keeps only the first reason (`http_message.h:149-150`), so a client that disconnects
before `stop()` consumes the cancel, and a later Shutdown is swallowed (the Limits bullet, `design:412-414`).
On plain TCP a half-closed client does the same at the first `on_cancel` (the watch peeks at once).

Authority: acceptable as a documented limit. The service is destroyed before the runtime (`main.cc:241`
vs `:278`), so the runtime is alive during `HTTP stop()` and answers `outcome_unknown` at the outcome
deadline T after submission (`service.cc:1780-1787`). Because `stop()` starts after submission, that is
at most T after `stop()` begins, inside the 5 s grace. The backstop's extra second matters only if the
runtime has failed, so the excess over the grace is at most 1 s and only for a failed runtime. The
Limits bullet overstates it ("6 s").

Classic clustered: the limit is not documented and is larger. A disconnected writer's waiter holds the
owner's 10 s timer (`detentd.cc:306`, D13.1); the swallowed cancel never wakes it, and the owner's
`stopping_` read applies to writes that have not started. `stop()` can wait up to 10 s, five seconds past
the grace. That is no regression (a blocked worker holds `stop()` the same way today), but the design
presents `stop()` as answering waiting requests by the contract.

Verdict: not a blocker. Close it cheaply or document both: `main.cc` calls a `begin_shutdown()` on the
handler before `service.stop()`, which sets `stopping_` and posts one settle-all to the owner, so no
waiter depends on a per-request cancel. If not, extend the Limits bullet to the classic 10 s wait and
add a T6 case, "disconnect, then `stop()`", asserting `stop()` returns within grace + 1 s (authority)
or documents the 10 s (classic).

### New findings

- **N1 (blocking for S4; keeps L3 PARTIAL).** Shutdown replies cannot leave through an owned stream
  (above). Fix: D8 says a wait that ends on Shutdown answers through the buffered writer; T1(b) and T6
  keep their rows, which then pass.
- **N2 (condition; C3).** S1 cannot build `assert_off_loop` on reactor `fe8c5ce`. Move the helper to S2
  after the pin, or have S1 carry it behind a compile-time stub; keep S1 to the hop and T1 (a)-(d).
- **N3 (condition; L2).** Pass `request.keep_alive` to the owned stream's `start` (default is
  `false`), and add a second-request row to T11.
- **N4 (non-blocking).** Say in S4 that `~Service` releases the work guard after `stopping_` is set.
- **N5 (condition; C4).** Make T10's second pattern emptiable: scope it to `src/detentd website
  docs/USAGE.md`, or exclude `docs/design`, `docs/reviews`, `docs/status` and `docs/FLEET_QUALIFICATION.md`.

- **N6 (condition; L1).** My Asio-only model of D2's cancel path hung after the emit (above). Make T1(b)
  (a Shutdown cancel reaches the owner wait and wakes it, with a channel and a timer in the race) the
  first test S1 lands, before S3 or S4 build on D2. If it hangs there too, the `||` race under a bound
  terminal slot needs a different shape (for example a flag polled by the timer tick).

Outside this scope, one line for the lead: the relay's `set_request_timeout(65 s)` (`host.cc:249`) and
the classic mode's request timeout of 0 (`main.cc:245,279`: `authority_network_timeout` stays 0 unless
`--authority`) are not covered by D8's timeline; the classic receive window is unbounded today.

Not run: Detent builds, suites, sanitizers, benchmarks, fuzzing; a TLS slow-reader timing finer than
"cut before the full body"; reverts of tests (there are none on the branch).

### Overall

L1 (design), L2, L4, L5 closed. L3 is PARTIAL on one concrete defect (N1) that the design's own T6 would catch at
S4; it is a text fix to D8/D10, not a redesign. C3 and C4 are PARTIAL on small, mechanical items (N2,
N5). Recommendation: **APPROVE WITH CONDITIONS** for S1, S2 and S3 to start (none depend on N1), with N1
fixed in the design before S4 starts, N6 answered by S1's T1(b), and N2, N3, N5 folded in the same revision.

## Re-check (revision 3)

Scope: `Detent:bf20f94`, `docs/design/http-coroutines.md` (read, not built), against task `cbfee34`
(`task.hh`, `task_asio.hh`) and http's `docs/design/task-handlers.md` revision 2 (`http:209ceef`).
Nothing was built or run: every claim below settled from the code.

### N1-N6

| ID | Verdict | Evidence |
|---|---|---|
| N1 | CLOSED | D8 `:295-300` and D10 `:323` answer a stop-token wait through the buffered writer; T1(b) and T6 keep their rows |
| N2 | CLOSED | D14 `:383-386` and S2 `:570` carry `assert_off_loop` with the reactor pin; S1 `:569` no longer lists it |
| N3 | CLOSED | D8 `:289-293` passes `request.keep_alive`; S4 `:572` repeats it; T11's second request is in the table at `:55` |
| N4 | CLOSED | D2 lifetime 2 `:189-191` and S4 `:572`: `stopping_`, then the guard, then the join |
| N5 | PARTIAL | T10 `:512` scopes the second pattern to `src/detentd website docs/USAGE.md`, which excludes the design, review and status docs. D11 `:343` makes a removed flag fail "with an error that names the removal"; that string lives in `src/detentd/main.cc` and contains `--workers`, so the pattern still matches after S5. Exempt the one rejection line (or its file) in T10, or have the check assert exactly one hit |
| N6 | CLOSED | S1 `:569` and T1 `:503` make (b) the first test, written and passing before (a), (c), (d), with (f) the mutant. Whether it passes is still S1's to show |

### Retargeted decisions

**D2 (owner hop): holds; every primitive exists with the behavior named.**

1. `schedule_on` (`task.hh:799-804`) posts the frame to the owner's scheduler. The child's
   continuation scheduler is the handler's; `final_awaiter` posts home when it differs (`:481-491`).
2. `event` (`:1201-1260`): the awaiter records `current()` and resumes through it; `set()` is
   idempotent, any-thread, and never resumes inline when a scheduler was recorded.
3. `deadline` (`:1108-1135`) takes `S&` with `start_timer` (`task_asio.hh:130`), which `asio_scheduler`
   has. Its source is linked to the parent: a parent already requested yields a born-requested
   child (`cancel_state::make`), so a stop before the hop is seen, with the parent's reason.
4. `cancel_callback` (`:1062-1103`) runs `f` at once when the token is already requested, so
   "register, then await" has no lost wake-up: `done` is set before the await and `await_ready` is true.
   A request racing the registration is covered by `add` failing and the immediate invoke. The
   waiter resumes on the owner (its recorded scheduler), which is also where the completion runs, so
   `settled` and `reply` need no lock.
5. Lifetime: the event lives in `pending`, held by the owner map, the lambda in the callback and the
   frame. The callback's destructor waits for a running `f` on another thread, so `begin_shutdown()`
   cannot touch a freed event. The frame's locals (`deadline`, `cancel_callback`) die at `co_return`
   on the owner thread, before the hop home, so the deadline is not destroyed off-loop in
   normal operation; on a forced teardown it is destroyed after the owner joined, and `~deadline`
   only disarms (`:1128`), with the timer's queued expiry holding the state. An event destroyed with
   a waiter marks it orphaned (Debug counter) but the waiter's frame destroys itself first (unlinks).
6. A `post` after `close()` is dropped (`task_asio.hh:68-71`); a dropped `schedule_on` leaves the
   child to the parent root's `destroy_roots()`. No leak path beyond D15's.

Citation nits (non-blocking): the rethrow in the child is `task.hh:505`/`:515`, not `:495` (that line
stores `unhandled_exception`); `:1195-1199` is the class comment, `set()` is `:1219`.

**D10 (`cancel_source` per service, `begin_shutdown()`): holds.** `cancel_source::request(reason)`
exists (`:1039`), first request wins, callbacks run on the requesting thread. The per-hop deadline's
parent is `stop_.token()`, so shutdown reaches the wait through the link and the reason is
`shutdown`, distinct from `deadline` for `truthful_reply`. Detent never reads
`request.cancel_token()`, so http's first-reason rule (task-handlers D5) cannot swallow it. The
ordering (`stopping_` set, then `request`) leaves no window in which a hop is accepted and unseen:
a hop starting after both is refused (`service.cc:791-793`); a hop that raced the request has its
token born requested.

**D15 (four phases): PARTIAL. The calls Detent needs are not exposed by http's design.**
Detent's `OwnerLoop` can run all four phases (`asio_scheduler::close()`/`destroy_roots()` exist,
`task_asio.hh:104-118`, with `assert(!runs_here())` and a stopped-loop requirement). But
task-handlers rev2 puts every phase inside `TcpServer::stop()`/`~UdpServer` (the disposition at
`task-handlers-rev2-design.md:385`), and its `HttpAsioService` "adds nothing"
(`task-handlers.md:72`): no separate stop, close or destroy-roots call exists in D1 or R2. D15 step
2-4 across the HTTP reactors and the owners therefore cannot be written as designed. Detent's
Limits note this only as a request (`design:476-480`, dependency 2). Consequence if http keeps
the single `stop()`: phase 3 runs on the reactors while an owner thread still runs, and a handler
child suspended on an owner (a stop path that skipped `begin_shutdown()`) is destroyed with its
parent root while the owner may touch its event. In normal operation D10 ends every owner wait
first, so T13's zero-live-frames row passes; the forced path is the exposed one. Two ways out for
the lead to rule on: (a) http exposes `stop()`, `close()`, `destroy_roots()` on `HttpAsioService`
(the design's current ask); (b) Detent stops and joins the owners first, after `begin_shutdown()`
has answered every waiter, and says in D15 that the HTTP stop then owns all of its own phases.
(b) needs no http change but makes a late hop onto a joined owner a stated refusal.

### New findings

None in the fix text. One line outside the scope: D11's rejection message vs T10 (above, N5).

### Verdict

N1-N4 and N6 CLOSED; N5 PARTIAL (T10 matches the rejection message); D2 and D10 hold against task's
API; D15 is PARTIAL until http exposes the phases or D15 takes option (b).

Not run: builds, suites, sanitizers, probes (none needed; the task claims settled from `task.hh`
and `task_asio.hh`), benchmarks; no mutant reverts (the branch has no tests).
