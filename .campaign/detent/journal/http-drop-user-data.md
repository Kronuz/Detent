# http-drop-user-data: journal

State: waiting on jobs 261119-145409-701207 261119-145429-db04cf 261119-145430-8ae0a9 261119-145432-2c024b 261119-145550-cc5355 261119-145612-62f648
Quest: .campaign/detent/quests/http-drop-user-data.md (the lead's; never edit it)

## Doing

`http:b931f28` on `quest/detent/http-drop-user-data` (pushed; CI runs there): the removal,
`Request::deadline()`, tests and docs in one commit. Jobs queued (below).

## Next

1. Read the jobs; fix any failure; check CI (TSan is CI's) on `http:b931f28`.
2. When http-affinity-death-tsan lands: merge http `main` (merge, never rebase), rerun the suite.
3. Cite the Xapiand lead's PLAN.md decision for A3 when the lead records it.

## Jobs

| Job | What | For |
|---|---|---|
| 261119-145409-701207 | http suite at `http:b931f28` | A1, A4 |
| 261119-145432-2c024b | `timeouts_test` at `http:b931f28` | A2 |
| 261119-145429-db04cf | mutant `http:8d52571` (no `set_deadline`, buffered path): `timeouts_test` must fail | A2 |
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
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet: Xapiand lead's decision> |
| A4 | <no evidence yet> |

## Result

<Not yet.>

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
