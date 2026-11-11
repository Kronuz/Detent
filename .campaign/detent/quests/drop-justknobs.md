# drop-justknobs: Detent on its own terms: remove the JustKnobs lineage

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status drop-justknobs`.

## Outcome

The current tree describes Detent on its own terms. No file names or describes Meta's JustKnobs, or
calls Detent a clone of it, an implementation of it, or inspired by it; no disclaimer about it
remains. "Knob" stays as the generic noun. The specification moves from
`justknobs-implementation-spec.md` to Detent's own `docs/design/knob-model.md`: knobs, namespaces,
groups, overrides, rollout and evaluation as Detent defines them today, written fresh from the code
and tests rather than edited from the old spec. one-knob-type later rewrites it for the single type
(D65). Git history is not rewritten.

## Acceptance criteria

- A1 A check finds no lineage: `tools/check_lineage.sh` greps the tree (excluding `.campaign/`) for
  `justknobs`, `just knobs`, and `Meta` as a company name (case-insensitive, `metadata`, `meta`
  file names and similar words excluded by word boundaries), and text in rendered diagrams (the
  PNG and SVG sources are re-rendered from their D2 or other sources); the job passes, and fails on a
  revert of any one removal (`--fails-without`).
- A2 [artifact] `docs/design/knob-model.md` specifies the model; README's opening paragraph and
  `docs/USAGE.md` point at it; the website's FAQ entry, copyright note and design-documents row
  about JustKnobs are gone, and `welcome.md` says what Detent is without comparison.
- A3 The site builds and its link check passes, and the suite passes (nothing links to the deleted
  spec): jobs.

## Contracts

None changed.

## Decisions

- D66 (owner, 2026-11-11): remove the JustKnobs lineage and keep the name Detent.

## May land early

None.
