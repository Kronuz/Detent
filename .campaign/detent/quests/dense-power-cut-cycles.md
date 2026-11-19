# dense-power-cut-cycles: Dense pages: the power-cut matrix cuts after kill, restart and republish

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-power-cut-cycles`.

## Outcome

dense-sync-stack's durability review (F1, F2, F3) found that the DTPAGE03 power-cut matrix has no kill, restart, republish, then cut cycle: kill cases are terminal and nested runs start from `durable(root)`, so bytes a kill left in the page cache are modelled as durable. A mutant that adopts an unreached name (f2, job 261118-...-4f96d1) survives every cut. After this quest the matrix models a kill's unsynced bytes as lost at the next cut, runs kill, restart, republish and cut, and kills f2 and the owner-unsynced mutant by a cut, not only by the order or counter checks.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The matrix runs kill -> restart -> republish -> cut, with a kill's unsynced bytes dropped at the cut: jobs.
- A2 Mutant f2 (adopt an unreached name) and the owner-unsynced mutant (95665a) each fail on a cut check: --fails-without jobs.
- A3 The suite passes.

## Contracts

None (tests only).

## Decisions

Filed 2026-11-18 from dense-sync-stack durability review F1-F3.

## May land early

None.
