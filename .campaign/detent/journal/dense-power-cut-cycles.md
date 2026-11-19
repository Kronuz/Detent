# dense-power-cut-cycles: journal

State: waiting on jobs 261119-055252-757710 261119-055255-9815e2 261119-055304-fe7712 261119-055306-ca033c 261119-055308-4179ea
Quest: .campaign/detent/quests/dense-power-cut-cycles.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Kill cycles and deferred counter checks are written in
`tests/detent_local/test_paged_publication_artifacts.cc` (T7 `power_cut::matrix`); mutant runner
`tests/detent_local/paged_power_cut_mutant.sh {f2|owners-unsynced}`. Jobs at quest/detent/dense-power-cut-cycles 0491457b:
- 261119-055252-757710 (A1, strict): the matrix in the suite's Debug build, 3 timed runs (after).
- 261119-055255-9815e2 (strict, base dcb09eb3, slot dpc-base): the same 3 timed runs (before).
- 261119-055304-fe7712 (A2, --fails-without): f2 must die at a "kill, restart, power cut".
- 261119-055306-ca033c (A2, --fails-without): owner-unsynced (95665a's edit) must die at a cut.
- 261119-055308-4179ea (A3): the suite.

## Next

1. Read the A1 timing job (after) against the base job (before); split gate/deferred if the cost is
   out of budget.
2. Fill Evidence from `campaign brief`; Result; ready for review.

## Blocked on

The lead (msg-97): the quest's file set names files that do not exist
(`test_local_publication_power_cut*`, `*power_cut*`, `agent_store_*`); the matrix is in
`tests/detent_local/test_paged_publication_artifacts.cc`, outside it. Landing needs that file added
(`campaign set dense-power-cut-cycles --files ...`). The work continues meanwhile.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The counter checks (startup, publication, total) move after the cuts, into `Run::check` beside
  the order check: 95665a's mutant drops a sync group, so the counters caught it before any cut.
- A kill restarts only when it selects the base (a kill after the rename selects the published
  generation, which has no next publication to make) and leaves bytes a cut would lose (its files
  differ from its durable-only cut); distinct kills are keyed by names plus staged and durable bytes.
- A kill's restart is cut with Entries {Durable, All} x Data::Zeros, as the cut restarts are.
- The A2 runner counts a mutant killed only when the test fails at a cut whose message names it
  (`kill, restart, power cut after event` for f2), so a counter or order failure does not count.
