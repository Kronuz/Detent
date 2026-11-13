# delta-plan-fixed-cost: A delta's plan has a store-sized fixed cost

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status delta-plan-fixed-cost`.

## Outcome

A block answer's plan time is proportional to the keys it changes, with no fixed part that grows with the store. Today, after delta-plan-scaling, the per-key part is flat (13.5-13.9 us per changed key at 100K, 1M and 5M), but a fixed part remains and grows with the store: at 5M an f 0.05 answer (0.047 C) plans in 13.80 s, against 0.28 s at 100K, and its whole apply (14.38 s) costs as much as a full copy (14.50 s), so a delta never pays at 5M (namespace-measure 5M round 261112-184757-fefadb, docs/perf/namespace-measure.md). The cause is found by profile and fixed, not worked around.

## Acceptance criteria

- A1 The cause, from a profile of an f 0.05 plan at 1M (D85), named by function and by what it walks or builds per store page rather than per changed key: a profile job and the journal's row
- A2 After the fix, an f 0.05 plan at 1M costs within 2x of its keys times the per-key figure (about 13.8 us), and one budgeted exclusive 5M round (arms full and 0.05 only) confirms it: a strict job at 1M and the 5M job
- A3 The published generations are byte-identical to main's for the same answers at 100K and 1M: a job comparing digests
- A4 The suite passes, and a counting probe shows the fixed part no longer grows with the store (operations at f 0.05 across 100K and 1M, not time): jobs, `--fails-without`
- A5 [artifact] Before and after per size and phase, appended to `docs/perf/delta-plan-scaling.md`

## Contracts

None changed: the paged format and published bytes stay identical.

## Decisions

D85, D87. Needs namespace-s7-ids, which holds the same files. Its result moves T9's alpha at 5M (namespace-measure).

## May land early

Nothing.
