# library-adoption-ae: journal

State: landed
Quest: .campaign/detent/quests/library-adoption-ae.md (the lead's; never edit it)

History: landed at `Detent:fbd15d5` on 2026-10-19, before this campaign; recorded with
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
| A1 | `docs/DEPENDENCY_UPGRADES.md`: "Compatible pins adopted, October 19" |
| A2 | `docs/DEPENDENCY_UPGRADES.md`: "A clean application build passes 201/201 tests in 957.12 seconds. The relay invocation passes 15/15 tests" |
| A3 | open: the release plan's required exit, carried to RC-1 and RC-5 (`docs/RELEASE_PLAN.md`) |

## Result

Landed as `Detent:fbd15d5` on 2026-10-19: 201 native tests and a 15-test relay run (13 new cases); M1 later moved every pin to its head (`m1-pins`). Docs: `docs/DEPENDENCY_UPGRADES.md`, `docs/LIBRARY_ADOPTION_ARCHITECTURE.md`.
Left open: A3.

## Rulings I made

None recorded.
