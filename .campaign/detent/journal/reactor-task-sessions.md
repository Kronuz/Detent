# reactor-task-sessions: journal

State: landed
Quest: .campaign/detent/quests/reactor-task-sessions.md (the lead's; never edit it)

## Doing

Nothing: landed at reactor:2fd30c8 on 2026-11-02 under the earlier campaign skill.

## Next

1. Nothing: landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/task-sessions.md`: "is empty; the bench's plain-Asio comparator (`benchmarks/echo_bench.cc:48-76`) is the one excepted use" |
| A2 | `docs/status/task-sessions.md`: "[x] T2 Suites: `ctest -N` lists head's nine" |
| A3 | `docs/status/task-sessions.md`: "[x] T3 Probes on reactor (Linux x86-64, ASan+UBSan and TSan;" |
| A4 | `docs/status/task-sessions.md`: "[x] T4 (i) `session_test` [I]: an idle session's read ends within two loop passes of `abort_all()` (0.17 ms)" |
| A5 | `docs/status/task-sessions.md`: "[x] T4 TH2-R1 / D15: [T] a root on server A suspended on server B's loop" |
| A6 | `docs/status/task-sessions.md`: "[x] T5 ASan+UBSan, TSan: separate builds, 11 of 11 pass each (clang 18, Linux x86-64)" |
| A7 | `docs/status/task-sessions.md`: "[x] K9 `live_frames()` back at its baseline after `stop()`" |
| A8 | `docs/status/task-sessions.md`: "[x] A mutant per decision dies (below)" |
| A9 | `docs/status/task-sessions.md`: "[x] 0 extra allocations per accepted connection versus head (below)" |
| A10 | `docs/status/task-sessions.md`: "[x] CI green on every job (see Log)" |

## Result

Landed at reactor:2fd30c8 on 2026-11-02. The landing commit records the step R re-check and the AbortList follow-up.

- Allocations per connection, echo with recycling on: 6.00 at head to 0.00 (one reactor) and 1.50 (shared acceptor).
- Review: `docs/reviews/task-sessions-code.md` -- APPROVE WITH CONDITIONS (CR1-CR12), fix round CR1-CR11 and the re-check.
- Docs: `docs/API.md`, `CHANGELOG.md`.
- Left open: none.

## Rulings I made

None beyond the quest file's Decisions (the ledger is the record).
