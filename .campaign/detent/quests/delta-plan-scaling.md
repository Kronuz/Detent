# delta-plan-scaling: A delta's plan grows superlinearly with the store

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status delta-plan-scaling`.

## Outcome

A block answer's plan time grows about linearly with the keys it changes, not with the store. Today the plan phase is superlinear: a 0.5 C delta plans in 0.75 s at 100K (50,035 keys, 15 µs per key) and in 512.8 s at 5M (2,499,245 keys, 205 µs per key; job 261112-144057-f1c829 at the warm-graph-5m fix), while the 5M full copy applies in 14.6 s (plan 10.6 s). A 50x larger change costs 680x the time, so at 5M a delta is 35x slower than a full copy and T9's cut point is meaningless until this is fixed (D87).

## Acceptance criteria

- A1 The cause, from a profile of the plan phase at 1M (D85: not 5M), named by function and by how its cost grows (the operation repeated per key, and what it scans or re-sorts): a profile job and the journal's row
- A2 After the fix, plan time per changed key at 100K, 1M and 5M stays within 2x of the 100K figure: a strict job at 100K and 1M, and one budgeted exclusive 5M round with arms full and 0.5 (D85)
- A3 The published generations are byte-identical to main's for the same answers at 100K and 1M (the fix changes cost, not bytes): a job comparing digests
- A4 The suite passes, and a test fails without the fix where a counting probe can show the growth (operations per key, not time): jobs, `--fails-without`
- A5 [artifact] Before and after numbers per size and phase: `docs/perf/delta-plan-scaling.md`

## Contracts

None changed: the paged format and published bytes stay identical.

## Decisions

D85, D87. Blocks namespace-measure's T9 rerun. Runs before namespace-s7-ids, which needs the same files.

## May land early

Nothing.
