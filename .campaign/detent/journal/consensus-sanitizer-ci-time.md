# consensus-sanitizer-ci-time: journal

State: waiting on jobs 261123-020912-132abe 261123-020851-6677d3 261123-020847-4f235f 261123-020845-ec263b and CI run 45153365600 (consensus-sanitizer-ci-time:47dcb27, main merged and pushed)
Quest: .campaign/detent/quests/consensus-sanitizer-ci-time.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Worktree ../consensus-wt/detent/consensus-sanitizer-ci-time. A1 committed (consensus-sanitizer-ci-time:0338620): CMakeLists.txt skips the mutant tests when CMAKE_CXX_FLAGS, CMAKE_CXX_FLAGS_<BUILD_TYPE> or CMAKE_EXE_LINKER_FLAGS holds -fsanitize (the shared CI sets them there). A1 job 261122-210931-de7473; scripts in ~/.scratch/detent/consensus-sanitizer-ci-time/. A2 probe job 261122-210949-fe3a3e (owe-order x5 under TSan with 4 burners) to read where the 10 s goes (the `arrived` until in owe_order, groups_test.cc).

A1 job 261122-210931-de7473 done at 0338620 (exit 0): mutant tests plain=17 asan-ubsan=0 tsan=0; total tests plain=132 asan-ubsan=115.

A4 (lead's addition) committed consensus-sanitizer-ci-time:48ced0e: driver_mutants.py and tools/mutants.sh require each pattern to match exactly once (mutants.sh was `in`, now count==1; driver_mutants.py already exited, but its caller in a job reads that as a kill), a `check` / `--check` mode applies every mutant without building, ctest entries consensus_mutants_apply_driver and _core run it in every build; checkpoint-drops-suffix pattern updated to core.h's make_unique. Jobs: apply-all 261122-212240-272db5, mutant run 261122-212244-62ba1e (--fails-without: driver_test must fail on it; script exits 0 on configure/build failure). A1 job ran at 0338620; rerun at HEAD if the table says stale.

## Next

1. When the jobs and CI run 45153365600 finish: fill the Evidence table, record linux-asan-ubsan wall time (limit 60 min) and `ci:` rows, then ready to land.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261123-020841-cdfe7f (47dcb27: mutant tests plain=17 asan-ubsan=0 tsan=0; exit 0) |
| A2 | 261122-210949-fe3a3e (5 of 5 pass under TSan, 4 burners, unfixed bound); 50-run job 261123-020845-ec263b |
| A3 | suite 261123-020847-4f235f; `ci:linux-asan-ubsan`, `ci:linux-tsan` (run 45153365600) pending |
| A4 | apply-all 261123-020851-6677d3; mutant run (fails without, exit 1 = killed, as designed) 261123-020912-132abe; earlier 261122-212240-272db5 and 261122-212244-62ba1e (that job's exit 1 was the kill: "the test failed without the fix, as it must", not a failure) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

A1 detects sanitizers from the flags (not a CONSENSUS option), so the shared CI needs no change.

A2: no owe-order failure reproduces locally (5 of 5, then 50-run job above); the CI abort (10.97 s, gate 10.1 s for 2,000 groups) was run 44993461293 on a loaded shared runner while mutants also ran in the same job; with mutants out of sanitizer builds (A1) the job is far less loaded. No fix made unless the 50-run job fails; no hunt.
