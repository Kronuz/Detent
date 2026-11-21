# paged-first-touch-one-shape: paged reads: one first-touch page check

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status paged-first-touch-one-shape`.

## Outcome

Two first-touch page checks exist, `LazyPageAccess::resolve` and `checked_page_bytes`; D42 DES-7 (reviews/dense-pages-design.md:77) merges them when reader-s6 rewrites `resolve`, but that went by message only and reader-s6's quest omits it. After this quest one check serves both paths.

## Acceptance criteria

- A1 One first-touch check remains (`checked_page_bytes` or `resolve`'s, not both), and a corrupt-page test fails at a mutant that skips it on either path (--fails-without): jobs.
- A2 First-read cost unchanged within 5% (first-read gate, before and after at named commits): jobs.
- A3 The suite passes: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U6).

## Folded tasks (one feature, owner msg-145/147)

This quest is one feature; each former quest below is a task, its file kept as the task's spec and its criteria folded into this quest's:
- `paged-decode-one-shape`: .campaign/detent/quests/paged-decode-one-shape.md
- `w2-leftovers`: .campaign/detent/quests/w2-leftovers.md
- `detent-review-nits`: .campaign/detent/quests/detent-review-nits.md
