# namespace-measure: Namespace: T9 cut point, T4(7) server cost, period-64 wire run

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-measure`.

## Outcome

The three measurements the design requires before S5.2 (`docs/design/namespace-delivery.md` First steps, namespace-design Next 2) are run and recorded: T9 sets α, T4(7)'s `completion_sync_probe` on APFS and a local Linux VM, and the period-64 wire run, which decides whether the roll-up period moves from 16 to 64. The decision rule for each is written in `docs/perf/namespace-measure.md` before the first run (pre-registered).

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 [artifact] The pre-registered decision rule for each of T9, T4(7) and the period-64 run, committed before its first job: `docs/perf/namespace-measure.md`: "## Rules"
- A2 T9 (D85): block answers of 0.05, 0.25, 0.5 and 1 C (uniform keys, 5% tombstones) and a full copy, five runs each on the 100K corpus and one budgeted round on the 5M corpus, exclusive; α at L = 1 and 10 Gb/s from each, with relay CPU and bytes read per answer; when the 5M α falls outside the 100K spread the 5M round decides and the verdict says it has no spread: jobs
- A3 T4(7) `completion_sync_probe` on APFS (exclusive) and in a local Linux VM (QUIET_VM): jobs
- A4 The period-64 wire run against period 16 (`wire2.out` shape): a job
- A5 [artifact] The verdicts (α, roll-up period) by each rule, and the design's numbers updated or confirmed: `docs/perf/namespace-measure.md`: "## Verdicts"

## Contracts

Provides α and the roll-up period that namespace-s5-2's layout policy uses.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. No AWS or rented machines: a local VM only.

## May land early

None: one result.
