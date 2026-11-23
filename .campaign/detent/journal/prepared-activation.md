# prepared-activation: journal

State: landed
Quest: .campaign/detent/quests/prepared-activation.md (the lead's; never edit it)

History: landed at `Detent:479c4bc` on 2026-10-27, before this campaign; recorded with
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
| A1 | `docs/reviews/prepared-activation-component-2026-10-26.md`: "uses owner-held `confirmed_prepared`, assigned after recovery settlement and applicable publication confirmation." |
| A2 | `docs/reviews/prepared-activation-component-2026-10-26.md`: "distinguishes explicit Absent empty-store liveness from Uncertain replacement." |
| A3 | `docs/reviews/prepared-activation-component-2026-10-26.md`: "Metadata probing uses the protected walker and recovered-directory identity. Foreground timeout closes and drains" |
| A4 | `docs/reviews/prepared-activation-component-2026-10-26.md`: "All nine focused native FreeBSD checks pass in 1.00 second." |
| A5 | `docs/reviews/prepared-activation-component-2026-10-26.md`: "All 400 SDK and 80 CLI checks pass. Resident first-read samples meet 1 ms for 200/200 live" |

## Result

Enabled as `Detent:479c4bc` on 2026-10-27; post-merge qualification recorded at `Detent:48d1cab`.
5M: preparation 117.609 s, restart 108.307 s; live first read median 0.715 ms, p99 0.835 ms, 200/200 below 1 ms; stopped 198/200.
Hosted run 40434580649: root alias and race fixture without skips on six configurations; FreeBSD 170 native and nine sanitizer checks.
Review: `docs/reviews/prepared-activation-component-2026-10-26.md`, source GO, managed default conditional on the gates above;
`docs/reviews/prepared-managed-qualification-2026-10-26.md` records the 5M qualification.
Docs: `docs/PREPARED_ACTIVATION_ARCHITECTURE.md`, `docs/measurements/prepared-managed-5m-20261026.json`.
Left open: none.

## Rulings I made

None recorded.
