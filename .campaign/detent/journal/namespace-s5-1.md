# namespace-s5-1: journal

State: working
Quest: .campaign/detent/quests/namespace-s5-1.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

First build of the implementation (State: `Namespace` entries, enabling and drop
administrative entries, directory map, image DTAIMG02; node: administrative entries;
tests: test_namespace_positions, test_namespace_cluster, test_namespace_power_cut).

## Next

1. Build and run the three new tests and the authority suite locally (light hold).
2. Write docs/status/namespace-s5.md (how it works, contract).
3. Suite job (native) and pins; push the branch for CI's sanitizers (D54); crash model at the final head.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <job id at the current code> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
