# warm-graph-5m: A delta on a 5M store fails warm validation

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status warm-graph-5m`.

## Outcome

A block answer applied to a published 5M-definition store publishes, instead of failing with `invalid warm paged graph` (`src/detent_local/paged_warm_validator.cc:13`). Found by namespace-measure's 5M round 261112-115045-0c8be1 (Detent:e0c9f450 harness `docs/perf/namespace-measure-t9.cc`): the full copy published (C 266 MB), then the 0.5 C delta arm failed; the same harness publishes deltas at 100K. The earlier 5M failure (`PagedCursor` on default 500K-record limits) was fixed on main by `paged_format_limits()`; this may be the same class (a bound sized for small stores) or a validator defect.

## Acceptance criteria

- A1 The cause, named by the failing `require` and the values that broke it, with a reproduction that fails on main: a job that fails at main and the journal's row naming it
- A2 A test that fails without the fix and passes with it, at a size that reaches the cause (a scaled-down store that hits the same bound, when one exists; 5M only when nothing smaller does, D85): jobs, `--fails-without`
- A3 The suite passes: a job
- A4 [manual] The harness's 5M delta arm publishes at the fix: one budgeted exclusive job, arms full and 0.5 only (261112-144057-f1c829 at 6a3d64f2; src and include are unchanged since, so it is not rerun at 5M, D85)

## Contracts

None changed: the fix keeps the paged format and the validator's checks; a check may be widened only to the bound the format already allows.

## Decisions

D43, D85. Blocks namespace-measure's T9 verdict (D87).

## May land early

Nothing.
