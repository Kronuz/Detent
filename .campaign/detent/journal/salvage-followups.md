# salvage-followups: journal

State: landed
Quest: .campaign/detent/quests/salvage-followups.md (the lead's; never edit it)

History: landed at `Detent:9abad85` on 2026-11-03, before this campaign; recorded with
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
| A1 | `docs/status/salvage-followups.md`: "Docs match the code: `Detent:955e97d` (`docs/DEPENDENCY_UPGRADES.md`, publication atomicity)." |
| A2 | `docs/status/salvage-followups.md`: "the shared hash equals the old loop on 25,376 identities" |
| A3 | `docs/status/salvage-followups.md`: "CI run 41737319246 at `Detent:0c1aa47` is green on all jobs" |

## Result

Landed through `Detent:9abad85` on 2026-11-03.
CI 41737319246 at `Detent:0c1aa47` green; macos-x86_64 needed a third attempt (flake 3).
Native, ASan and UBSan pass the marks and identity tests. No review recorded.
Docs: `docs/DEPENDENCY_UPGRADES.md`.
Left open: none.

## Rulings I made

None.
