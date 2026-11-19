# logger-slack-mutant: journal

State: waiting on jobs 261119-144619-45023b (A1), 261119-144622-6f467c (A2)
Quest: .campaign/detent/quests/logger-slack-mutant.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

logger:6b4fd39 (quest branch, pushed): `test/deferred.cc` mode `slack` (ctest `logger_deferred_slack`) holds a
deferred line so the LOG thread sleeps to W, then logs a line due W+5 ms (inside kWakeSlackNs) and one due
W+50 ms, counting `worker_notifies`: near >= 10 of 20 rounds, far == 0. `test/deferred_mutant.sh` gains mutant
`slack` (`wake_for(ordered ? due : due - kWakeSlackNs)` -> `wake_for(due)`, run with `slack`).

## Next

1. On the jobs: A1 passes iff all four mutants die; A2 is the suite (includes the new test on the fixed tree).
2. If the near count flakes under load, loosen to >= 1 round (the mutant gives 0 always).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | job 261119-144619-45023b (`sh test/deferred_mutant.sh`) |
| A2 | job 261119-144622-6f467c (suite) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

Test lives in deferred.cc (file set), not wake.cc: bidirectional notify counts (near notifies, far never) so a too-wide slack is also caught.
