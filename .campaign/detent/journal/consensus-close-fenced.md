# consensus-close-fenced: journal

State: waiting on jobs 261117-223709-25c2a0 261117-223712-5a12b1 261117-223717-5549cc 261117-223724-63db40
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:9b218a0 (branch pushed): driver.h erases a closed group in pump() after its
step (ready list unlinked if still queued), open() loses the closed-row path, settle() returns
after fail_all closes the group (it read the freed Worker), `groups()` counts rows. Tests in
test/driver_close_test.cc. Jobs build the test by hand from driver_test's ninja commands: it has
no CMakeLists line (outside the file set).

## Next

1. Read the four jobs; fix what fails.
2. A `--fails-without` job: revert only the driver.h erase, settle and open hunks (keep groups()).
3. Fill the Evidence table; ready for review.

## Blocked on

CMakeLists.txt needs one registration for test/driver_close_test.cc (outside my file set), after
consensus_driver in the `if(CMAKE_SOURCE_DIR ...)` block:
    add_executable(consensus_driver_close_test test/driver_close_test.cc)
    target_link_libraries(consensus_driver_close_test PRIVATE consensus::consensus)
    add_test(NAME consensus_driver_close COMMAND consensus_driver_close_test)
    set_tests_properties(consensus_driver_close PROPERTIES TIMEOUT 120)
Lead: approve and I add it, or add it yourself. Until then the suite and CI do not run the test.

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

1. Erase in pump() after step(), not inside close_step: close_step runs under step()/settle()/
   fail_all(), which still hold `g` and the Worker reference.
2. The ready list is unlinked by a scan only when the node is still queued at erase (a witness's
   own apply marks it ready in the same step); the common close leaves it unqueued.
3. Dropped the closed-row branches (open's erase, live(), close(), state()): a closed group no
   longer outlives its step, so they were dead. Added `groups()` for A3.
