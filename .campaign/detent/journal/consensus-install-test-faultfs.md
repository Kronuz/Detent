# consensus-install-test-faultfs: journal

State: waiting on jobs 261121-222240-06c746 261121-222221-9081aa; CI on bf429c0
Quest: .campaign/detent/quests/consensus-install-test-faultfs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Pushed consensus:bf429c0 (driver_install_test on faultfs). Waiting on the A1 jobs and CI.

## Next

1. When both jobs and CI are green: write "ready for review" and fill the Evidence rows.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261121-222240-06c746 (ctest -R consensus_driver_install, the five cases); 261121-222221-9081aa (the `crash` sweep, outside ctest); `git grep memory_io` in consensus: empty |
| A2 | `ci:linux-tsan`, `ci:linux-asan-ubsan`, `ci:freebsd-x86_64`, `ci:macos-arm64`, `ci:macos-x86_64`, `ci:linux-arm64-gcc`: green at consensus:bf429c0 (run 44993461293; TSan rerun after an unrelated owe-order abort, consensus-sanitizer-ci-time) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

Crash seed is a per-node counter (deterministic). Power cut k is power_off_after(k - 1), dead() is mutations since the cut >= k, as in driver_close_test. Span counts mutations (faultfs has no read operations to count). CMakeLists: driver_install added to the faultlab link loop.
