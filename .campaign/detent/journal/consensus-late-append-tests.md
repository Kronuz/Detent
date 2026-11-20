# consensus-late-append-tests: journal

State: waiting on jobs 261119-165818-4fe4e7 261119-165835-4605e6 261119-165837-e49853 261119-165838-a838b5
Quest: .campaign/detent/quests/consensus-late-append-tests.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Tests at consensus:a292fd0 (test/consensus_test.cc): late_append_read_probe (A1), late_append_during_snapshot (A2). Jobs: plain 4fe4e7 (must pass first; mutants pass falsely if the plain test fails), A1 mutant 4605e6 (drops acknowledge_read in the late path), A2 mutant e49853 (drops `peer.snapshot ||` guard), suite a838b5. Mutant jobs exit 0 on setup/build failure so those fail the job.

## Next

1. Check the four jobs; read mutant logs to confirm only the new check fails; then ready for review.

## Blocked on

Nothing.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
