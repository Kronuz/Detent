# http-task-handlers: journal

State: landed
Quest: .campaign/detent/quests/http-task-handlers.md (the lead's; never edit it)

## Doing

Nothing: landed at http:fd29d35 on 2026-11-03 under the earlier campaign skill.

## Next

1. Nothing: landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/task-h2.md`: "[x] K1 / T1 one shape:" |
| A2 | `docs/status/task-h2.md`: "[x] K2 / T2 suites: every ctest passes, none removed" |
| A3 | `docs/status/task-h2.md`: "[x] K3 / T3 all 13 L3 probes, ported to `task::task` handlers" |
| A4 | `docs/status/task-h1.md`: "[x] K4 T4 (a)-(h) without the streamed cases, each failing with its decision mutated" |
| A5 | `docs/status/task-h2.md`: "[x] K5 / T5 sanitizers, separate builds, 0 reports" |
| A6 | `docs/status/task-handlers.md`: "measured on Linux metal, B1 inside the band or better on every cell, both compilers" |
| A7 | `docs/status/task-handlers.md`: "pinned as a ctest (T7): measured 2.00 against 2.00" |
| A8 | open: left unticked in the workstream ledger in `docs/status/task-handlers.md` |
| A9 | open: left unticked: the owner rules D8 in `docs/status/task-handlers.md` |
| A10 | `docs/status/task-handlers.md`: "[x] S-K1 the bench reports CPU/request, p99, allocations/request and handlers/request for A" |

## Result

Landed at http:fd29d35 on 2026-11-03. Design http:b110118, reviews http:88f1275, S1-S2 http:03467b1; the T6/T7 results http:fd29d35 (2026-11-03).

- Reviews: `docs/reviews/task-handlers-design.md` -- REJECT (revision 1); revision 2 reviewed in `docs/reviews/task-handlers-rev2-design.md` (APPROVE WITH CONDITIONS) on the unmerged `design/task-handlers` branch; `docs/reviews/task-handlers-xapiand.md` (the Xapiand lead).
- The `design/task-handlers` branch (http:694fb47, 4 commits) is not on main: it holds revision 2 of the design and its review.
- Docs: `docs/perf/t6-t7-2026-11-03.md`, `docs/design/task-handlers.md`.
- Left open: A8, A9.

## Rulings I made

None beyond the quest file's Decisions (the ledger is the record).
