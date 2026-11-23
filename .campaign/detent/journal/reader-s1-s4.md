# reader-s1-s4: journal

State: landed
Quest: .campaign/detent/quests/reader-s1-s4.md (the lead's; never edit it)

History: landed at `Detent:02675d3` on 2026-11-03, before this campaign; recorded with
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
| A1 | `docs/status/reader-s1-s4.md`: "S1 counters behind `DETENT_READ_COUNTERS` (`Detent:3cae505`)." |
| A2 | `docs/status/reader-s1-s4.md`: "S2: `test_local_evaluation` pins values and matches `detail::evaluate` on 4,680 combinations." |
| A3 | `docs/status/reader-s1-s4.md`: "The writer test reads values back through `PreparedReader` across 6 revisions" |
| A4 | `docs/status/reader-s1-s4.md`: "S1 mutants, both killed: dropping the `pages_verified` count; `table_bytes` off by one." |
| A5 | `docs/status/reader-s1-s4.md`: "and `test_testing_generation_writer`; all pass with no sanitizer report." |
| A6 | `docs/status/reader-s1-s4.md`: "Note: `docs/mechanisms/read-counters.md`." |

## Result

Landed `Detent:a8836d2`, `a7aebaf`, `63cf665`; ledger `Detent:02675d3` on 2026-11-03.
S1: first `a:k` lookup 2 segment opens, 5 pages verified, 1,466 bytes hashed; 0 differing object lines with the flag off.
S2: 4,680 combinations equal; `view.o` not byte-identical, so no performance claim. S4: six revisions and a reopened store.
No separate review recorded; the ledger says CI was pending and records no result.
Docs: `docs/mechanisms/read-counters.md`, `evaluation.md`, `generation-writer.md`.
Left open: none.

## Rulings I made

See Decisions.
