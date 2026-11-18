# container-selection: journal

State: landed
Quest: .campaign/detent/quests/container-selection.md (the lead's; never edit it)

History: landed at `Detent:4f1a086` on 2026-10-24, before this campaign; recorded with
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
| A1 | `docs/status/container-selection.md`: "Contributor tip `19cffc5` is reconciled." |
| A2 | `docs/status/container-selection.md`: "passes its seven-job matrix. The candidate uses this immutable pin and a forwarding header" |
| A3 | `docs/status/container-selection.md`: "A clean public-pin native build passes 224/224 checks. Rust and Python each pass five binding checks." |
| A4 | `docs/status/container-selection.md`: "source `a686fe1`, passes all six enabled jobs" |
| A5 | `docs/status/container-selection.md`: "passes 400 repetitions across four concurrent drivers" |
| A6 | `docs/status/container-selection.md`: "Two isolated-account fixture corrections pass 1,000 cleanup repetitions and 100 controlled-refund repetitions" |

## Result

EX-0 landed at `Detent:4f1a086` on 2026-10-24: the private sorted-vector engine is gone; `flat-map:a9a52b0` serves it.
Native 224/224 on a clean public-pin build; bindings 5/5 each in Rust and Python; hosted run 40125876413 six of six jobs.
Lifecycle stress 40125771247: 400 repetitions, four concurrent drivers; fixture corrections 1,000 and 100 repetitions.
Reviews: `docs/reviews/container-extraction-architect-2026-10-24.md` (approve the flat-map extension, reject the btree cast);
`docs/reviews/container-adoption-code-2026-10-24.md` (accept, conditional on clean public-pin validation, which item 3 supplies).
Docs: `docs/CONTAINER_SELECTION_REPORT.md`, `docs/CAPACITY_QUALIFICATION.md`, `docs/EXTRACTION_CHECKLIST.md` (EX-0 complete).
Left open: none.

## Rulings I made

None recorded beyond the reviews.
