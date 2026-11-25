# legacy-h-lifecycle-docs: D7 lists every per-namespace transition, slot-reuse pages and D155 pages say what the code does

Risk: low (docs). The lead writes this quest; contributors read it and never edit it. Its machine state is
in `state.json`: `campaign status legacy-h-lifecycle-docs`.

## Outcome

legacy-h's round-3 review (`.campaign/detent/reviews/legacy-h-correctness.md`, Detent:ff409c69)
approved the code and left three documentation findings: F6, D7's table in
`docs/design/client-local-source.md` misses four transitions (re-add at the old slot, agent restart,
a re-add A4 refuses, drop or re-add while an attach fails) and states two rules wrongly; F7, three
pages say slots are never reused, but the agent reuses them; F8, D155's pages leave out that
`refresh()` returns OK while another refresh runs or a pinned view refuses it. Fix the text to match
the code; the review's probes and local logs
(`.scratch/detent/review-legacy-h-correctness/local/`) show the behaviour.

## Acceptance criteria

- A1 [artifact] `docs/design/client-local-source.md` D7 has a row for each of F6's four transitions
  and the two corrected rules, each row naming the test that checks it.
- A2 [artifact] No page says slots are never reused (F7): a grep job over docs and website.
- A3 [artifact] D155's pages state refresh()'s OK-while-busy and pinned-view cases (F8).
- A4 The docs build passes and `ci:ci` is green at the landed commit.

## Contracts

None.

## Decisions

Lead, 2026-11-24: split from legacy-h so its accepted code lands at once. legacy-h review N2 (F3's
slot check no longer guards anything) is left as it is: harmless, and removing it touches the
reviewed mechanism.

## May land early

Not applicable.

## Tasks

None.
