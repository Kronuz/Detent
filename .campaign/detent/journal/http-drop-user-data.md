# http-drop-user-data: journal

State: blocked on lead: the file set lacks the transport, handler and test files the removal touches
Quest: .campaign/detent/quests/http-drop-user-data.md (the lead's; never edit it)

## Doing

Nothing: survey done (below), no code changed. http branch `quest/detent/http-drop-user-data` is
at `http:ec273dd` (main), 0 commits.

## Next

1. On the widened file set: remove `Request::user_data` (`http_message.h:132-135, 195`), add
   `Request::deadline()` + transport setter, set it at `http_asio.h:987` and `:1102` from
   `request_deadline` (`:955`, `time_point::max()` when `request_timeout` is 0).
2. Remove `DeferredReply`, `detail::DeferredExchange` (`http_deferred.h:96-285`),
   `ResponseWriter::defer()` (`http_handler.h:41-42, 105-107`), `AsioDeferredControl` and the
   writer's deferred path (`http_asio.h:112-157, 172-185, 202-211, 248, 282, 454, 698, 741-770`).
   Keep `ReplySubmit`, `ReplySettlement`, `ReplyLimits`, `DeferredOptions`, `validate_reply`:
   owned streams use them (`StreamOptions::reply`, `http_owned_stream.h:12, 55`).
3. Tests: delete `test/deferred_test.cc`, `test/deferred_asio_test.cc` and their CMake entries;
   rewrite `test/readahead_test.cc:741-790` [U] on an owned-stream control; drop
   `test/coroutine_test.cc:467-490, 800-808` [J]; move `test/asio_test.cc:71-94` and
   `examples/kv_store.cc:32, 104-119` from `user_data` to `Request::extension`; a deadline test in
   `test/timeouts_test.cc` (A2) with a mutant that leaves the setter out.
4. Docs: `AGENTS.md:34`, `ARCHITECTURE.md:52`, `http_request_parser.h:152` comment, CHANGELOG.
5. Jobs: suite (A4, A1); Detent and Xapiand builds with `FETCHCONTENT_SOURCE_DIR_HTTP_LIB` at the
   branch (A1).

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

1. Lead: the file set. The removal cannot build within it: `http_handler.h` declares `defer()`,
   `http_asio.h` implements it and is where the deadline is known, and four files outside the set
   use the removed names. Options: (a) add `http_handler.h`, `http_asio.h`, `http_request_parser.h`,
   `AGENTS.md`, `examples/kv_store.cc`, `test/asio_test.cc`, `test/coroutine_test.cc`,
   `test/readahead_test.cc`; (b) split into "add `deadline()`" and "remove the two surfaces".
   Recommendation: (a), one change, one review.
2. Lead, routing to other repositories: http-log `README.md:94` and `docs/API.md:22` (docs);
   pin bumps in Detent, Xapiand, prism after landing.
3. Xapiand lead: A3, the removal decision (PLAN.md Decisions id).

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
   without naming the type, and a rename widens the break for no consumer. A rename to
   `ReplyOptions` is a one-line follow-up if the lead wants no "deferred" left.
2. `Request::deadline()` is the transport's per-request bound (`request_timeout` from the start of
   the request's read), `steady_clock::time_point::max()` when unbounded; `clear()` resets it.
