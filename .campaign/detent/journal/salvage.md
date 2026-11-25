# salvage: journal

State: landed
Quest: .campaign/detent/quests/salvage.md (the lead's; never edit it)

History: landed at `Detent:5207f96` on 2026-11-03, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/salvage-2026-11-03.md`: "Commits (47 in 13 heads): 45 ALREADY ON MAIN, 1 SUPERSEDED (`f7efb55`)" |
| A2 | `docs/status/salvage-2026-11-03.md`: "Three pieces land on `chore/salvage`; nothing else is left to land." |
| A3 | `docs/status/salvage-2026-11-03.md`: "carry nothing that is not on `main`, on a pushed track branch, or recorded here as dropped" |

## Result

Landed `Detent:0d20dc1`, `22cf5db`, `b1c8422`; ledger through `Detent:5207f96` on 2026-11-03.
Method: `git cherry` and patch-id against every main commit, file and line checks, `git apply --check`.
CI 41714654828 at `Detent:09fd0b3` passes eight jobs.
No review recorded.
Left open: none.

## Rulings I made

Decisions per item are in the ledger's tables.
