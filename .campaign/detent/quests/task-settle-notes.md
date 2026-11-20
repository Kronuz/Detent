# task-settle-notes: task: same-thread settle test and the cancel-callback wait rule

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status task-settle-notes`.

## Outcome

drivers-task-wheel's review parked F6 and F7 for journal-driver or consensus-driver, and neither took them (reviews/drivers-task-wheel-correctness.md:117-118): task:95798a0 has no `inline-nested` same-thread settle case, and `docs/API.md` does not state the cancel-callback wait rule. After this quest both exist.

## Acceptance criteria

- A1 An inline-nested same-thread settle test passes, and fails at a mutant that settles re-entrantly (--fails-without): jobs.
- A2 [artifact] The cancel-callback wait rule: `docs/API.md`.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R3).
