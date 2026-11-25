# http-affine-thread: journal

State: landed
Quest: .campaign/detent/quests/http-affine-thread.md (the lead's; never edit it)

## Doing

Nothing: landed at http:73cfa02 on 2026-11-03 under the earlier campaign skill.

## Next

1. Nothing: landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/affine-thread.md`: "`BodyStream::affine()` asserts `scheduler_.runs_here()`: the rule is the loop thread, not the scheduler's identity." |
| A2 | `docs/status/affine-thread.md`: "[x] Test `coroutine_test` [T] (h) `/affine-wrapped`: a handler under a wrapper scheduler" |
| A3 | `docs/status/affine-thread.md`: "[x] Death tests `/affine-body`, `/affine-read` (another scheduler on another thread)" |
| A4 | `docs/status/affine-thread.md`: "[x] Mutant: empty `affine()` in both: the three death tests fail." |
| A5 | `docs/status/affine-thread.md`: "[x] Debug ASan+UBSan and TSan builds: ctest 13/13 each." |
| A6 | `docs/status/affine-thread.md`: "[x] Docs: AGENTS.md, CHANGELOG.md, `docs/mechanisms/coroutine-handlers.md`." |

## Result

Landed at http:73cfa02 on 2026-11-03. Landed as http:6fe31df and http:73cfa02 (2026-11-03); CI run 41639438922 green on 8 jobs.

- Review: none (low risk).
- Docs: `docs/mechanisms/coroutine-handlers.md`.
- Left open: none.

## Rulings I made

None beyond the quest file's Decisions (the ledger is the record).
