# seeding-waits: journal

State: landed
Quest: .campaign/detent/quests/seeding-waits.md (the lead's; never edit it)

History: landed at `Detent:1cc2f38` on 2026-10-23, before this campaign; recorded with
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
| A1 | `docs/status/seeding-waits.md`: "Serialized file and directory sync completion occupies most measured seed time." |
| A2 | `docs/status/seeding-waits.md`: "Both 10K current-source runs applied 80 transactions and verified the full 10,000-record state." |
| A3 | `docs/status/seeding-waits.md`: "Do not change durability or library defaults from this evidence alone." |

## Result

Landed `Detent:f7d92f0` and `Detent:1cc2f38` on 2026-10-23.
100K mixed seed 104.338 s; saved telemetry 71.84 s to the first post-seed health snapshot.
10K: 5.52 s and 5.92 s at the last in-seed and first post-seed snapshots; follow-up seed 10.363 s, storage driver 7.254 s.
No review. Docs: `docs/perf/seeding-waits.md`.
Left open: none.

## Rulings I made

None.
