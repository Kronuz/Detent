# namespace-s5-2: Namespace S5.2: ring, seals, compacted blocks, roll-ups, cut, bases, checkpoint at the seal

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s5-2`.

## Outcome

S5.2 (`docs/design/namespace-delivery.md` D6, T4): the authority persists compacted blocks. The set-limits entry; the layout policy in `Limits` with α as a ratio of integers (from namespace-measure); consensus limits and the retained-bound check; the ring keyed by namespace position with lazy eviction; seals on changes or log progress; seal files, roll-ups, squash and cut; bases and the first table at the enabling entry; the commit, collector, recovery and repair from a peer; the checkpoint captured in apply at the seal's index (`node.cc:519-530` today) with a durable layout; install; the three answers.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 T4(1): every seal file, block and base is byte-identical on every voter, also with a ring capture failure on every voter at one position, a policy change mid-run, a voter restarted mid-period and one installed mid-period: jobs
- A2 T4(2): the ring evicts only at its bound and only entries the durable layout covers; after restarting every voter, every answer for an R at or above the cut is unchanged: a test
- A3 T4(3): the layout follows ratio 4, L, the deduplicating merge, the cost cut and the disk cap, and disk per namespace stays within the design's bound: a test that checks the sizes
- A4 T4(4): a voter installed from a snapshot fetches the table, bases, blocks and seal files and answers the same bytes: a test
- A5 T4(5): crash matrix at every primitive of a seal commit, a roll-up's outputs, a table publication (the first included), the collector and an install, also with the period changed mid-run: the crash-model job
- A6 The three answers (ring, blocks, base) are selected by the cost cut with namespace-measure's α: tests with the bytes sent per answer
- A7 [decision] α and the roll-up period come from namespace-measure's verdicts: a PLAN.md decision row

## Contracts

Provides `namespace-blocks`: the seal-file, block, base and table formats and the three answers as D6 states them. S6 serves them; S8 mirrors them byte for byte.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. A format quest: the docs that describe these files belong to it, not to the docs quests.

## May land early

None: the format and its crash matrix are reviewed together.

## Inputs from S5.1's review (non-blocking)
- S1: fold the probe's acknowledged-step check (.scratch/review-namespace-s5-1-durability) into the suite's power-cut test.
- S2: a suite test that a drop resets the change history floor (an old cursor gets no deltas).
- S3: image decode checks that the directory head equals the highest position.
