# task-cancellation: journal

State: landed
Quest: .campaign/detent/quests/task-cancellation.md (the lead's; never edit it)

## Doing

Nothing: landed at task:6558c2e on 2026-11-02 under the earlier campaign skill.

## Next

1. Nothing: landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/cancellation.md`: "[x] K1 Every decision's T1 tests pass, and each fails with its fix reverted" |
| A2 | `docs/status/cancellation.md`: "[x] K2 The review's probes rerun clean on the branch headers (T3)" |
| A3 | open: left unticked: T4 is the lead's after C3 in `docs/status/cancellation.md` |
| A4 | `docs/status/cancellation.md`: "[x] K4 Zero live frames: `live_frames()` back to baseline in every T1 (D8) test" |
| A5 | open: left unticked: green at task:1c30b8b and task:a40bfe3, the landing commit is later in `docs/status/cancellation.md` |
| A6 | open: not run (C3 pin) in `docs/status/cancellation.md` |
| A7 | open: empty in task; Xapiand's at its wave commit in `docs/status/cancellation.md` |
| A8 | `docs/status/cancellation.md`: "[x] K8 (added per re-check CN-R3) D8.4 unlocks in the awaiter's destructor only when the waiter's state is `granted`" |
| A9 | `docs/status/cancellation.md`: "[x] K9 (added per re-check NF1) D5.3: before `initiating` is set, a cross-thread cancel only records the request" |
| A10 | `docs/status/cancellation.md`: "[x] K10 (added per re-check NF2, NF3) `~event` keeps a Debug count of orphaned waiters" |
| A11 | `docs/status/cancellation.md`: "[x] K11 (added per re-check TH2-L1 note and the lead) D8.7 states `close()` and" |
| A12 | `docs/status/cancellation.md`: "[x] K12 (added by the lead from http S1) T7 can count task frames against asio's" |

## Result

Landed at task:6558c2e on 2026-11-02. C1 task:b406766, C2 task:d3d94f3, C3 task:6558c2e (re-check closed every finding), 2026-11-02.

- C3 fix round: four macOS builds green, 42 mutants caught; CI green on 8 jobs at task:a40bfe3 (run 41579535675).
- Reviews: `docs/reviews/cancellation-c1-code.md`, `cancellation-c2-code.md`, `cancellation-c3-code.md` -- each APPROVE WITH CONDITIONS, conditions closed.
- Later on main: task:d080786 (each scheduler scope its own trampoline loop), task:fefcda4 and task:fb7d9bf (how-it-works notes).
- Docs: `docs/design/cancellation.md`, `docs/mechanisms/cancellation.md`, `docs/mechanisms/await-asio.md`, `docs/mechanisms/asio-scheduler-teardown.md`.
- Left open: A3, A5, A6, A7.

## Rulings I made

None beyond the quest file's Decisions (the ledger is the record).
