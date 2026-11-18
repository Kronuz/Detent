# http-task-h1: http buffered path on task (H1)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at http:2bede1b (2026-11-02); its ledger `docs/status/task-h1.md` in Kronuz/http is the record. The quest's repository is `../http`. Its machine state is in `state.json`: `campaign status http-task-h1`.

## Outcome

http's handler and router API, the connection factories and TLS run on `task::task`: the handler is a child task of the connection, the session token and deadlines bound reads, `Request::cancel_token()` engages a lazy watch, deferred and owned writes cancel, an unanswered cancelled request gets 503, and the service tears down in four phases.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] D1 handler and router API, factories and TLS on `task::task`: `docs/status/task-h1.md`
- A2 [artifact] D2 the handler is a child task; no post, driver or shared block; mutant D2-post killed: `docs/status/task-h1.md`
- A3 [artifact] D3/D4 session token and `task::deadline`s; mutants killed: `docs/status/task-h1.md`
- A4 [artifact] D5 `cancel_token()` and `cancel(reason)`, lazy watch; the old cancel API gone; mutants killed: `docs/status/task-h1.md`
- A5 [artifact] D7 deferred and owned writes cancel; D8 503 + close; D9 Debug affinity assert; D10 pins task C3 and reactor R: `docs/status/task-h1.md`
- A6 [artifact] Teardown ruling: `stop()`, `close()`, `destroy_roots()` each running the earlier ones: `docs/status/task-h1.md`
- A7 [artifact] K1 http's share of one shape; K2 13/13; K4 T4 (a)-(h) without the streamed cases: `docs/status/task-h1.md`
- A8 [artifact] K5 ASan+UBSan and TSan clean on macOS and Linux; CI sanitizer jobs green: `docs/status/task-h1.md`
- A9 [artifact] K7 allocations and io handlers per request at most A; K9 zero live frames after teardown: `docs/status/task-h1.md`
- A10 [artifact] K10 CI green on every job (run 41585840133); consumer break list recorded: `docs/status/task-h1.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Lead rulings 2026-11-03 recorded in the ledger (bench_asio, the handler's scheduler).
- Risk high: request lifetime and cancellation on every HTTP path (the auditor's mapping).

## May land early

None.
