# http-drop-user-data review: correctness

Commit: http:b931f281b4854eb0504de9f811fc945fefee65dd
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-145409-701207: http suite at b931f28, exit 0 (A1, A4).
- 261119-145432-2c024b: `timeouts_test` at b931f28, exit 0; mutants 261119-145429-db04cf
  (buffered, no `set_deadline`) and 261119-145430-8ae0a9 (streamed) exit 1 under
  `--fails-without`, so each `set_deadline` call is load-bearing for the test (A2).
- 261119-145550-cc5355 (Detent build and ctest) and 261119-145612-62f648 (Xapiand build): both
  carry `--ref HTTP=http@b931f281b485`, so they build against this commit (A1).
- PLAN.md D127 for A3.

## What I checked myself

1. Removed surface (question 1). Read the whole diff. Nothing still reachable depended on it,
   and every removal fails to compile rather than changing behaviour silently:
   `defer()`, `DeferredReply`, `user_data`, `ReplySubmit::{AlreadyCompleted,Closed}` and
   `DeferredExchange` are all gone, and `git grep` finds no use of them in http (outside
   design/review/history records), Detent `bf14d9a7`, Xapiand `d7d5e1cd0`, prism `4d3c96d` or
   http-log `ebe1a9c` source. The owned stream still reads all four `DeferredOptions` fields
   (`http_owned_stream.h:36,55,224-225,247,268`), so none became a field that is silently
   ignored. `ConnectionInput` now has one `observer_`
   (`http_asio.h:382-383,598-599,625`), the same mechanism as before. `map_handler_error` and the
   writer's destructor keep the stream's abandon, close and settle path unchanged. The transform
   skip (`serialize_head`, `http_asio.h:179`) only ever applied to deferred replies; an owned
   stream never reaches `serialize_head`.
2. `deadline()` (question 2). `request_deadline` is computed inside the serve loop
   (`http_asio.h:852-854`), once per request, and set after `take_headers()` (885) and `take()`
   (1001). Keep-alive reuse and pipelining therefore each get a fresh deadline, counted from the
   end of the previous reply. The timeout cannot change while the service runs:
   `set_request_timeout` throws after start (`http_asio.h:1097-1102`). `clear()` resets the
   deadline to `max()`, and the parser's own `request_` is never given one.
   Probe 261119-153801-f4488c failed 3 checks. The fault was in the probe, not in http. The
   probe expected the second keep-alive request's deadline to come at least 550 ms after the
   first one's. That assumed the 600 ms idle wait moves the deadline, but the deadline is set at
   the loop top, right after the first reply. The first reply took under 1 ms, so
   `d2-d1=0` is the right answer. The run's own `d2-t2=1392` (2002 minus the ~610 ms
   between the sends) also shows idle time counts against the deadline. The `2002 > 2000`
   failure is a tolerance fault: the server's loop top runs a couple of ms after the client's
   `connect()` returns, and `ms_of` truncates.
   Probe 261119-155346-bc9393 holds the first keep-alive reply for 300 ms and widens that
   tolerance to 2050 ms. It passes with 0 failures:
   - buffered keep-alive `d2-d1=301`, streamed keep-alive `d2-d1=301`;
   - pipelined `d2-d1=301` (buffered) and `303` (streamed);
   - `d1-t1=2000`, `d2-t2≈1395`.

   So the deadline is fresh per request and is not inherited. The same run shows the body
   hooks see `max()` and that a buffered reply finished 300 ms past the deadline is still
   written.
3. Test removals (question 3). `deferred_test`'s old cases all exercised `DeferredExchange` and
   went with it. Its `validate_reply` coverage grew: 204/304 with a body, the 200-599 status
   range, an empty name, tab acceptance. I mapped each `deferred_asio_test` case to the behaviour
   that still exists:
   - Owned-stream timeout, abandon and shutdown within 2 s: `owned_stream_asio_test`.
   - The monitor's EOF and reset rule: `readahead_test` [U], now on `AsioOwnedStreamControl`.
   - An exception after the capability is taken: `coroutine_test` (g) `/owned-cancel`.
   - Keep-alive after an owned stream: `owned_stream_asio_test` `transfer()`.
   - Not covered end to end any more: see N4.
4. Docs (question 4). The CHANGELOG entry matches the new surface. Present-tense docs outside
   design/review/history records still describe deferred replies: see D1.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| D1 | yes | defect | Present-tense docs still describe the removed surface. AGENTS.md lists the deleted test and "deferred replies" (`AGENTS.md:55-57`), and also has a "Deferred replies" entry (`:72`) and a "closes a deferred or owned-stream reply's exchange" line (`:157`). README links TRANSPORT.md as "deferred replies" (`README.md:175`). TRANSPORT.md says the input waits while "a deferred response waits" and "notify[s] the current deferred exchange" (`docs/TRANSPORT.md:10`). The coroutine-handlers doc says "a deferred or owned-stream reply closes with `Shutdown`" (`docs/mechanisms/coroutine-handlers.md:165`). All of these files are in the quest's file set. | `git grep -i deferred b931f28 -- '*.md'` excluding docs/design, docs/reviews, docs/history and docs/status | Drop the deferred wording at those six places (owned stream only), and drop `deferred_asio_test.cc` from the AGENTS.md list. The suite at the fixed commit confirms nothing else moved. |
| G1 | no | gap | The A2 test covers only fresh `Connection: close` requests. Keep-alive reuse and pipelining are correct by inspection (per-iteration `request_deadline`, `http_asio.h:852`), but a mutant that sets the deadline once per connection would survive. | probe 261119-155346-bc9393 passes (keep-alive and pipelined, buffered and streamed); 261119-153801-f4488c's failures were probe faults (see point 2) | Optional: fold the probe's keep-alive and pipelined checks into `timeouts_test`. |
| N1 | no | nit | `create_extension`, `on_request_body` and `wants_body_stream` run inside the parser, before `set_deadline` (`http_asio.h:885,1001`), so `deadline()` is `max()` there. Neither the hooks' comments nor the CHANGELOG say so. | `http_request_parser.h:67,115,153`; probe f4488c prints what `wants_body_stream` saw | Say so in `Request::deadline()`'s comment, or set the deadline on the parser's request before feeding it. Owner's call. |
| N2 | no | nit | "When the transport ends this request" overstates. The handler is not cut at the deadline. On plain TCP, `send_reply`'s `send_now` writes a late buffered reply without checking it (`http_asio.h:836-846`); only reads and the rest of the write are bounded. | probe f4488c `[past]` line | Reword to "the bound the transport applies to the request's reads and the rest of its reply". Pre-existing transport behaviour; the wording is new. |
| N3 | no | nit | `std::string serialize_head(bool)` (`http_asio.h:172`) lost its only caller with `write_response`. No caller remains in http, Detent, Xapiand, prism or http-log, so two shapes of one function are left. | `git grep "serialize_head("` | Remove the overload. |
| N4 | no | gap | Two `deferred_asio_test` cases now have no end-to-end analog for owned streams: a pipelined request queued behind an owned-stream reply, and a half-close before an owned-stream reply. Unit tests cover the mechanism: the loop is sequential, and `readahead_test` [U] checks the monitor's EOF rule. | `test/owned_stream_asio_test.cc`; `test/readahead_test.cc:741-792` | Optional: add both to `owned_stream_asio_test`. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- Fix round http:0549709 (suite 049d80, which I did not rerun). I checked only its diff:
  - It clears the six D1 places; `git grep -i deferred` there now finds only the
    `http_deferred.h` name.
  - It rewords `deadline()`'s comment, which covers N1 and N2.
  - It removes the `serialize_head(bool)` overload, which covers N3.

  It changes code comments and removes one dead overload, so it is not docs only. I did not
  review it beyond that.
- I did not check CI (TSan included) at b931f28 myself; I took the journal's word for it.
- No sanitizer or Linux runs of my own.
- I did not review the http-log doc lines or the pin bumps; they are routed to the lead.
