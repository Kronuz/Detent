# relay-snapshot-lifecycle: journal

State: landed
Quest: .campaign/detent/quests/relay-snapshot-lifecycle.md (the lead's; never edit it)

History: landed at `Detent:c5680e4` on 2026-10-23, before this campaign; recorded with
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
| A1 | `docs/status/relay-snapshot-lifecycle.md`: "regression fails against frozen baseline libraries and passes against the candidate" |
| A2 | `docs/status/relay-snapshot-lifecycle.md`: "Native tests cover live-history eviction, snapshot building during upstream staging" |
| A3 | `docs/status/relay-snapshot-lifecycle.md`: "The candidate full-product suite passes 219/219 tests in 749.14 seconds." |
| A4 | `docs/status/relay-snapshot-lifecycle.md`: "The added real TLS delayed-consumer test passes for raw and compressed snapshots." |
| A5 | `docs/status/relay-snapshot-lifecycle.md`: "the combined delayed TLS case. Item 4 supplies that missing evidence" |
| A6 | `docs/status/relay-snapshot-lifecycle.md`: "The 500-session sharing fixture and 100K TLS fixture pass after correcting plan-owner funding (4/4, 11.80 seconds)." |
| A7 | `docs/status/relay-snapshot-lifecycle.md`: "The scoped LLVM 23 ASan/UBSan build passes the native and delayed TLS regressions" |

## Result

Landed as `Detent:c5680e4` on 2026-10-23, squashing `609fc1c`, `28d1c8c`, `36addc1`, `6063daf` (identical code diff).
Full-product suite 219/219 in 749.14 s; TLS delayed consumer B=42/C=48 decodes 42, 45, 48, then history loss after C, 3/3.
Fixtures 4/4 after correcting plan-owner funding; scoped ASan/UBSan 3/3 (no LeakSanitizer on macOS).
Review: `docs/reviews/relay-bootstrap-code-2026-10-23.md`, APPROVE WITH CONDITIONS; CR1 met by the TLS case.
Docs: `docs/RELAY_ARCHITECTURE.md`. No hosted CI run is recorded in the ledger.
Left open: none.

## Rulings I made

Retirement funding includes the configured session bound; completed plans release at their final acknowledgment.
