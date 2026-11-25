# stage-3-authority-runtime: journal

State: landed
Quest: .campaign/detent/quests/stage-3-authority-runtime.md (the lead's; never edit it)

History: landed at `Detent:238bd92` on 2026-10-04, before this campaign; recorded with
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
| A1 | `docs/AUTHORITY_ARCHITECTURE.md`: "Complete bounded authority component, with consolidated Architect source GO and native/sanitizer qualification." |
| A2 | `docs/AUTHORITY_ARCHITECTURE.md`: "Seven authority ASan/UBSan tests pass, including an actual HTTP daemon ACK followed by SIGKILL/restart/retry" |
| A3 | `docs/IMPLEMENTATION.md`: "[complete bounded authority component](AUTHORITY_ARCHITECTURE.md) has consolidated Architect source GO" |

## Result

Landed as `Detent:238bd92` on 2026-10-04: 30 native Detent tests, seven authority ASan/UBSan tests (an HTTP ACK then SIGKILL, restart and retry included), eight root cluster tests; the review's reproduced defects corrected (receiver activation, overwritten waiters, semantic profiles, duplicate JSON members). Docs: `docs/AUTHORITY_ARCHITECTURE.md`.
Left open: none.

## Rulings I made

None recorded.
