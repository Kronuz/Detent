# consensus-mutants-per-test: journal

State: waiting on jobs 261121-173829-dc0b56 (A1 local macOS) and the branch's CI run (A3)
Quest: .campaign/detent/quests/consensus-mutants-per-test.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

consensus:e35356a pushed: tools/mutants.sh takes [NAME] (slug) and --list; CMake adds 17
consensus_mutant_<slug> tests only where Ninja and bash exist (find_program), with a configure-time
check that the script's list equals the tests'. Old consensus_mutants removed.

## Next

1. A1: job 261121-173829-dc0b56 (macOS, ctest -R consensus_mutant_ -j4); Linux is CI's (A3).
2. A3: CI on e35356a; if a sanitizer job times out a mutant, raise that TIMEOUT from the logged time.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | job 261121-173829-dc0b56 (macOS); Linux/sanitizers in CI |
| A2 | `CMakeLists.txt`: the consensus_mutants list; each TIMEOUT is 4x the slowest of msg-190's macOS/Linux/FreeBSD time, floor 600 s |
| A3 | `ci:linux-tsan`, `ci:linux-asan-ubsan`, `ci:freebsd-x86_64`, `ci:macos-arm64`, `ci:macos-x86_64`, `ci:linux-arm64-gcc`: all green at consensus:e35356a; 17 of 17 mutants killed on macOS in 73-420 s (dc0b56) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- FreeBSD: tests added only where bash is found (smaller than porting the bash-array script to POSIX sh); FreeBSD CI skips the mutants.
- TIMEOUT 4x the slowest platform (floor 600 s), not 2x: sanitizer jobs time unknown, and the script already bounds each suite itself.
- The 17th mutant (a read counts every acknowledgement) has no msg-190 time: 900 s guess, confirm from the A1 job.
- NAME is the lower-case dash slug (`mutants.sh --list`), so test names have no spaces or quotes.
