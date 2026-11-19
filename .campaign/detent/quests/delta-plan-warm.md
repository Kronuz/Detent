# delta-plan-warm: delta plan: measure a warm second plan on one mapping

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status delta-plan-warm`.

## Outcome

D110 closed delta plan scaling with a cold plan and left "a warm second plan on one mapping" as a follow-up measurement, which no quest took. After this quest the warm second plan's cost is measured beside the cold one and recorded.

## Acceptance criteria

- A1 Cold and warm second plan on one mapping at 500K and 5M, at a named commit (strict): jobs.
- A2 [artifact] The results and their reading: `docs/perf/delta-plan-scaling.md`.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U7).
