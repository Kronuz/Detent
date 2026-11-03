> Status: CURRENT (2026-11-03) how an authority command, read or result reaches the runtime and how its reply leaves, read from `src/detent_authority/service.cc` at the head of `phase/w2-s2`.

# Authority request path

`detent::authority::Service` is an `http::HttpHandler`; every route is a task on the HTTP reactor's loop.
The runtime (consensus, storage, encoding) is one long task on the service's `OwnerLoop`.

## Admission and intake

`create_extension` admits the request (credential, per-producer and global counts, subscription lanes)
and returns an `Extension` holding the `Admission`; any refusal is a `RequestRejected` with Detent's JSON
body before a body byte is read. `GET /v1/health` with a body header is refused the same way
(`health_body_forbidden`). Subscription bodies go to their lane lease through `on_request_body`; every other
body is buffered in `Request::body` under the service's `set_max_body(request_bytes)`.

## The hop

`handle` parses and validates on the loop (`prepare`), then `co_await`s `detent::owner_submit<Outcome>`
(`owner-loop.md`):

1. `accept` (owner): `enqueue` takes `mutex_`, refuses with `stopping` or `producer_busy`, or pushes the
   `Task` and installs `complete`, which stores `Task::outcome` and sets `Task::done`.
2. The runtime settles the task through `complete` on its own thread, which is the owner thread.
3. `finish` (`reply_for`): the settled reply, or `outcome_unknown` when the wait ended on the stop or the
   backstop (the outcome deadline plus one second). The reply is enveloped and reserved against
   `reply_bytes`; over the budget a command answers `outcome_unknown` and a read `admission_full`.

## The reply

A reply after a submission leaves through an owned stream created when the reply is ready:
`start(metadata, length, request.keep_alive)`, one `publish`, `finish`, with a write deadline of
`max(now, outcome deadline) + 1 s`. The stream retains the admission and the reply reservation until its
write settles. A wait that ended on `begin_shutdown()` is answered through the buffered writer, because the
transport aborts owned streams first. Refusals, parse errors and health stay buffered.

## Shutdown

`begin_shutdown()` marks the service stopping and requests the stop source; waits end with their truthful
reply. `drain()` waits for the runtime to settle every task and leave its loop; only then do the owner's
phases run (`teardown.md`). The destructor drains and runs the phases itself.

## Tests

`tests/detent_authority/test_service.cc` (admission, quota, replay, and `begin_shutdown()` with sixteen
readers waiting), `test_distribution_ownership.cc`, `test_transport_https.cc` (a reply pipelined over TLS
through an owned stream), all through `tests/support/handler_harness.hh`.
