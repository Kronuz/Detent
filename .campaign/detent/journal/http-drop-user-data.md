# http-drop-user-data: journal

State: waiting on jobs 261119-171647-04581a 261119-171648-48d8c6 261119-171651-1b0234
Quest: .campaign/detent/quests/http-drop-user-data.md (the lead's; never edit it)

## Doing

Merged http `main` (`http:d2e1819`, http-affinity-death-tsan) at `http:35116b0`, pushed; clean
merge: coroutine_test keeps main's TSan/NDEBUG skip of the three death tests and drops
`/defer-poll`. Queued at `http:35116b0`: suite 261119-171647-04581a (A1, A4), `timeouts_test`
261119-171648-48d8c6 (A2), Detent build + suite with http at `http:35116b0` 261119-171651-1b0234
(A1).

## Next

1. Cite the three jobs in Evidence when they pass; check CI at `http:35116b0`.

## Jobs

| Job | What | For |
|---|---|---|
| 261119-145409-701207 | http suite at `http:b931f28` | A1, A4 |
| 261119-145432-2c024b | `timeouts_test` at `http:b931f28` | A2 |
| 261119-145429-db04cf | mutant `http:8d52571` (no `set_deadline`, buffered path): `timeouts_test` must fail; done, it fails on exactly "a buffered request's deadline is its request timeout" | A2 |
| 261119-145430-8ae0a9 | mutant `http:b13278b` (no `set_deadline`, streamed path): `timeouts_test` must fail | A2 |
| 261119-145550-cc5355 | Detent `ee5b62c2` build + suite with `FETCHCONTENT_SOURCE_DIR_HTTP_LIB` at `http:b931f28` | A1 |
| 261119-145612-62f648 | Xapiand `1a933d883` `xapiand` build with http at `http:b931f28` | A1 |

Mutant commits are kept at `refs/mutants/http-drop-user-data-deadline-{1001,885}` in http (not pushed).

## Callers of the removed surface (2026-11-19, `git grep` of every Kronuz checkout)

Consumers of http: Detent (pin `http:c813531`, `cmake/dependencies.json:70`), Xapiand (pin
`http:eb08b59`, `CMakeLists.txt:830`), prism (pin `http:d76c5ef`, `CMakeLists.txt:22`), http-log
(pin `http:c813531`). cpp-libraries, reactor and task mention http in docs only.

| Repository | `user_data` | `DeferredReply` / `defer()` | Change it needs |
|---|---|---|---|
| Detent `main` | none | none (CI step `ci.yml:70` already forbids both) | none; owned streams (`StreamOptions::reply`) stay. Pin bump after landing |
| Xapiand `main` | none | none | none in source; pin bump; A3 agreement (`xapiand-74`) |
| prism `main` | none | none | none; pin bump optional |
| http-log `main` | none | docs only | `README.md:94`, `docs/API.md:22`: drop "`defer()` or" |
| http itself | `examples/kv_store.cc`, `test/asio_test.cc` | `test/coroutine_test.cc`, `test/readahead_test.cc` | steps 1-3, outside the file set |

Stale Detent worktrees `resource-adoption`, `variant13`, `followups` still name `user_data` or
`defer(`; they rebase onto Detent `main`, which has neither.

## Blocked on

Nothing. File set widened (option (a) granted). Routed to the lead: the http-log doc lines
(`README.md:94`, `docs/API.md:22`) and the Detent, Xapiand and prism pin bumps, after landing.
A3: the lead asked the Xapiand lead; the reply becomes a PLAN.md decision to cite.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-154010-049d80 (http suite at `http:0549709`); Detent build + suite with http at `http:b931f28`: 261119-145550-cc5355; Xapiand `xapiand` build: 261119-145612-62f648 (0549709 changes docs and drops an uncalled overload only) |
| A2 | 261119-155820-ab2ecb (queued, `timeouts_test` at `http:0549709`); fails without: 261119-145429-db04cf (buffered), 261119-145430-8ae0a9 (streamed) |
| A3 | Detent `.campaign/detent/PLAN.md` D127 (Xapiand lead's agreement, msg-124) |
| A4 | 261119-154010-049d80; CI at `http:0549709` running |

Correctness review: accepted at `http:0549709` (D1 confirmed by 049d80; the probe's keep-alive
failures were the probe's, rerun 261119-155346-bc9393 0 failures).

## Result

`http:b931f28`: `Request::user_data`, `ResponseWriter::defer()`, `DeferredReply`,
`detail::DeferredExchange` and the transport's deferred path are gone (-575/+132 lines);
`Request::deadline()` is set on the buffered and streamed paths. Docs: `CHANGELOG.md`
(2026-11-19), `ARCHITECTURE.md` "Response ownership", `docs/TRANSPORT.md`,
`docs/mechanisms/coroutine-handlers.md`, `docs/OWNED_STREAMING.md`. Detent and Xapiand build
unchanged against it; the pin bumps and http-log's two doc lines are the lead's.

## Rulings I made

1. `DeferredOptions` keeps its name: `StreamOptions::reply` is one, Detent sets its fields
   without naming the type, and a rename widens the break for no consumer. Renaming it to
   `ReplyOptions` is a follow-up if the lead wants no "deferred" left. `http_deferred.h` keeps
   its name for the same reason (owned streams include it); it holds only the reply options now.
2. `Request::deadline()` is the transport's per-request bound (`request_timeout` from the start of
   the request's read, which on keep-alive is the end of the previous reply),
   `steady_clock::time_point::max()` when unbounded; `clear()` resets it.
3. `ReplySubmit` drops `AlreadyCompleted` and `Closed`: only `DeferredExchange::complete` returned
   them; no consumer names `ReplySubmit`.
4. `test/deferred_test.cc` now tests `detail::validate_reply`, the rest of `http_deferred.h`, and
   keeps the transport-owned-header coverage the deferred test had. `deferred_asio_test.cc` goes.
5. The writer's helpers become `is_streamed()` / `abandon_stream()`; `write_response` goes and
   `respond` calls `write_owned_response`. Internal to `http_asio.h`, no consumer uses them.
6. The transform skip for deferred replies goes with them: every buffered reply runs the
   conditional, range and compression transforms, as non-deferred replies already did.
7. `docs/AUDIT.md`'s "Deferred reply footprint" section goes (it measured the removed member);
   design docs and reviews under `docs/design`, `docs/reviews`, `docs/history` stay as records.
8. `test/coroutine_test.cc`: only the `/defer-poll` route, its [J] case and the header line
   naming it change, per the lead's instruction.
