# stage-1-raft-safety: journal

State: landed
Quest: .campaign/detent/quests/stage-1-raft-safety.md (the lead's; never edit it)

History: landed at `Detent:e78dc25` on 2026-10-02, before this campaign; recorded with
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
| A1 | `docs/IMPLEMENTATION.md`: "Four initial heartbeat checks failed on the preceding library source. Thirteen expanded checks pass after correction" |
| A2 | `docs/IMPLEMENTATION.md`: "Implementation authorized; legacy safety prerequisites validated locally" |
| A3 | `docs/IMPLEMENTATION.md`: "Both two-node and three-node discovery, replication/remote, and distributed-search harnesses pass" |

## Result

Recorded at `Detent:e78dc25` on 2026-10-02 ("Record final legacy consensus qualification"): seven corrections in cluster (`23407e8`, `7c7dd86`, `6f6fcc3`, `1697e56`, `5b2a141`, `5f583e5`, `510e120`), Architect GO or review on each; all three cluster CTests native and ASan/UBSan, 23 Detent CTests; Xapiand `877f46ab2` fixes the `-DLUA=OFF` stub and passes its multi-node harnesses. Docs: `docs/IMPLEMENTATION.md` (Stage 1).
Left open: none.

## Rulings I made

None recorded.
