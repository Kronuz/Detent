# consensus-sanitizer-ci-time: journal

State: waiting on jobs 261122-210931-de7473 261122-210949-fe3a3e 261122-212240-272db5 261122-212244-62ba1e
Quest: .campaign/detent/quests/consensus-sanitizer-ci-time.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Worktree ../consensus-wt/detent/consensus-sanitizer-ci-time. A1 committed (consensus-sanitizer-ci-time:0338620): CMakeLists.txt skips the mutant tests when CMAKE_CXX_FLAGS, CMAKE_CXX_FLAGS_<BUILD_TYPE> or CMAKE_EXE_LINKER_FLAGS holds -fsanitize (the shared CI sets them there). A1 job 261122-210931-de7473; scripts in ~/.scratch/detent/consensus-sanitizer-ci-time/. A2 probe job 261122-210949-fe3a3e (owe-order x5 under TSan with 4 burners) to read where the 10 s goes (the `arrived` until in owe_order, groups_test.cc).

A4 (lead's addition) committed consensus-sanitizer-ci-time:48ced0e: driver_mutants.py and tools/mutants.sh require each pattern to match exactly once (mutants.sh was `in`, now count==1; driver_mutants.py already exited, but its caller in a job reads that as a kill), a `check` / `--check` mode applies every mutant without building, ctest entries consensus_mutants_apply_driver and _core run it in every build; checkpoint-drops-suffix pattern updated to core.h's make_unique. Jobs: apply-all 261122-212240-272db5, mutant run 261122-212244-62ba1e (--fails-without: driver_test must fail on it; script exits 0 on configure/build failure). A1 job ran at 0338620; rerun at HEAD if the table says stale.

## Next

1. Read the probe, fix owe-order's bound or stall, run 50 of 50 (a2.sh 50).
2. Push, A3 CI, Evidence table.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

A1 detects sanitizers from the flags (not a CONSENSUS option), so the shared CI needs no change.
