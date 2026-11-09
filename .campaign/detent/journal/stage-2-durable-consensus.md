# stage-2-durable-consensus: journal

State: landed
Quest: .campaign/detent/quests/stage-2-durable-consensus.md (the lead's; never edit it)

History: landed at `Detent:5778aeb` on 2026-10-04, before this campaign; recorded with
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
| A1 | `docs/IMPLEMENTATION.md`: "The additive sibling `cluster/journal/` module, committed as cluster `55e3072`" |
| A2 | `docs/IMPLEMENTATION.md`: "The additive deterministic strict core and semantic journal adapter are committed as cluster `7b1e9ba`." |
| A3 | `docs/IMPLEMENTATION.md`: "seeded schedules cover 1,000 steps each with drops, duplicates, reordering, partitions, and durable restarts" |
| A4 | `docs/SNAPSHOT_TRANSFER_ARCHITECTURE.md`: "Final consolidated Architect review gives GO after five concrete corrections." |

## Result

Completed at `Detent:5778aeb` on 2026-10-04 with cluster `99d01be`: Journal suites 98,254 checks native and ASan/UBSan; 36,171 Core and 92,227 Worker checks; all eight root cluster CTests and 23 Detent CTests. Docs: `docs/IMPLEMENTATION.md` (Stage 2), `docs/SNAPSHOT_TRANSFER_ARCHITECTURE.md`.
Left open: none.

## Rulings I made

None recorded.
