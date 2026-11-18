# http-task-handlers: http handlers on task: design, S1-S2 and the T6/T7 gate (W2a)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at http:fd29d35 (2026-11-03); its ledger `docs/status/task-handlers.md` in Kronuz/http is the record. The quest's repository is `../http`. Its machine state is in `state.json`: `campaign status http-task-handlers`.

## Outcome

W2a's design and gate: `docs/design/task-handlers.md` (revision 2 on `design/task-handlers`), the bench counters and A/A script (S1), task pinned at head (S2), and T6/T7 measured on Linux metal against head's A/A band; the build steps H1 and H2 have their own quests.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] K1 handler, routes and `BodyStream::read` are `task::task`; no awaitable or shim (T1): `docs/status/task-h2.md`
- A2 [artifact] K2 every ctest at head passes, none removed (T2): `docs/status/task-h2.md`
- A3 [artifact] K3 the L3 probes rerun on the new API under ASan and TSan (T3): `docs/status/task-h2.md`
- A4 [artifact] K4 T4 (a)-(f) pass, each failing with its fix reverted: `docs/status/task-h1.md`
- A5 [artifact] K5 ASan+UBSan and TSan separate builds clean; g++ Debug ASan (T5): `docs/status/task-h2.md`
- A6 [artifact] K6 req/s and p99 at 8 and 64 connections within head's A/A band on Linux metal (T6, `docs/perf/t6-t7-2026-11-03.md`): `docs/status/task-handlers.md`
- A7 [artifact] K7 coroutine frame allocations per request no worse than head (T7): `docs/status/task-handlers.md`
- A8 [artifact] K8 http-log and prism migrated and pinned; Detent and Xapiand build against it (T8): `docs/status/task-handlers.md`
- A9 [artifact] K9 D8 ruled from K6 and K7; docs and the CHANGELOG upgrade note: `docs/status/task-handlers.md`
- A10 [artifact] S-K1 to S-K4: bench counters, the A/A script, task pinned at head, ledger current: `docs/status/task-handlers.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Owner ruling (2026-11-02): one shape everywhere; http's connection loop moves to task too (Detent `docs/SHARED_LIBRARY_DECISIONS.md`).
- K1-K5 are ticked in the step ledgers `docs/status/task-h1.md` and `docs/status/task-h2.md`, which this quest's rows cite.
- Owner questions Q1 (scheduler ownership), Q2 (`cancelled()` beside `cancel_token()`), Q3 were open when S1-S2 stopped; revision 2 answered them for H1.
- Risk high: the handler API of every HTTP consumer (the auditor's mapping).

## May land early

None.
