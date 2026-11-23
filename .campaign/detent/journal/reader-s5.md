# reader-s5: journal

State: landed
Quest: .campaign/detent/quests/reader-s5.md (the lead's; never edit it)

History: landed at `Detent:d1e9ee6` on 2026-11-04, before this campaign; recorded with
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
| A1 | `docs/status/reader-s5-s6.md`: "[x] `change` file in the agent: store word, per-entry sequences, grow-only" |
| A2 | `docs/status/reader-s5-s6.md`: "[x] Startup bump after the barrier, before ready." |
| A3 | `docs/status/reader-s5-s6.md`: "[x] Increments after the rename and barrier of every `selected` replacement." |
| A4 | `docs/status/reader-s5-s6.md`: "[x] Exclusive `owner.lock` while the agent writes the store." |
| A5 | `docs/status/reader-s5-s6.md`: "[x] Readers refuse a `change` the publisher does not own (D4 rule 4)." |
| A6 | `docs/status/reader-s5-s6.md`: "local tests, mutants, ASan/UBSan and TSan green" |
| A7 | `docs/status/reader-s5-s6.md`; CI run 41817708223 green at Detent:d4dbbad8f, read on GitHub on 2026-11-08 (the ledger was written before it ran) |

## Result

Landed `Detent:b547256`, `97da162` (reader-pressure budget 1,056 to 1,072 bytes), `d4dbbad`, `d1e9ee6` on 2026-11-03/04.
Mutants M1-M8 all killed; ASan/UBSan and TSan 5/5 each; CI 41817708223 green (freebsd and headers skipped by plan).
No separate code review recorded; the reader design review covers D4.
Docs: `docs/mechanisms/change-file.md`, `docs/design/reader.md` (D4's `owner.lock` wording). S6 continues as `reader-s6`.
Left open: none.

## Rulings I made

See Decisions.
