# task-cancellation: task cancellation, deadlines, inline resume and teardown (C1-C3)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at task:6558c2e (2026-11-02); its ledger `docs/status/cancellation.md` in Kronuz/task is the record. The quest's repository is `../task`. Its machine state is in `state.json`: `campaign status task-cancellation`.

## Outcome

Kronuz/task has one cancellation mechanism: `cancel_token` with a reason and lazy callbacks (std::stop_callback lifetime), deadlines as token sources, `await_asio` that takes a token and resumes inline on the scheduler's thread, and teardown phases that destroy queued frames, built in three steps C1 to C3 to `docs/design/cancellation.md` revision 2.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] K1 every decision's T1 tests pass and each fails with its fix reverted (D1-D8): `docs/status/cancellation.md`
- A2 [artifact] K2 the review's probes rerun clean on the branch headers (T3): `docs/status/cancellation.md`
- A3 [artifact] K3 the existing suite and benches in rel, ASan, TSan and g++ Debug ASan; T4 rows reported by the lead: `docs/status/cancellation.md`
- A4 [artifact] K4 zero live frames after every D8 test: `docs/status/cancellation.md`
- A5 [artifact] K5 CI green on every job at the landing commit: `docs/status/cancellation.md`
- A6 [artifact] K6 consumer rebuild (T6): strata, prospect, residency; Xapiand by its lead: `docs/status/cancellation.md`
- A7 [artifact] K7 T7 greps empty (`as_awaitable`, `lock_until`/`lock_for`) in task and at Xapiand's wave: `docs/status/cancellation.md`
- A8 [artifact] K8 D8.4 unlocks in the awaiter's destructor only when granted (`grant_timedout`): `docs/status/cancellation.md`
- A9 [artifact] K9 D5.3 a cross-thread cancel before `initiating` only records the request: `docs/status/cancellation.md`
- A10 [artifact] K10 `~event` counts orphaned waiters; the ownership death test: `docs/status/cancellation.md`
- A11 [artifact] K11 D8.7 `close()` and `destroy_roots()` idempotent; Limit 3 stated and tested: `docs/status/cancellation.md`
- A12 [artifact] K12 `TASK_FRAME_POOL=0` lets T7 count frames against asio's: `docs/status/cancellation.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Agreed between the Detent and Xapiand leads (Detent `docs/SHARED_LIBRARY_DECISIONS.md`, November 2): Detent owns the task and reactor changes; the Xapiand lead reviews (`docs/reviews/cancellation-xapiand.md`, XN1-XN3).
- The architect's REJECT of revision 1 (http `docs/reviews/task-handlers-rev2-design.md`), re-check CLEARED WITH CONDITIONS (`docs/reviews/cancellation-design.md`); Q1-Q3 ruled (a).
- C3's top-level ruling: `await_asio` resumes inline only at the top level (task:d8e1f27).
- Risk high: the family's one coroutine and cancellation mechanism (the auditor's mapping).

## May land early

None.
