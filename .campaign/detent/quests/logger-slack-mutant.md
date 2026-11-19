# logger-slack-mutant: logger: a mutant for the deferred lines' 8 ms slack

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status logger-slack-mutant`.

## Outcome

logger-burst-cost left no test that notices a dropped 8 ms slack on deferred lines (logger-burst-cost journal:91, F2). After this quest a mutant that drops the slack is killed by the suite.

## Acceptance criteria

- A1 A mutant dropping the 8 ms slack is killed (job passes iff killed): jobs.
- A2 The suite passes: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J7).
