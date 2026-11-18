# consensus-close-fenced: journal

State: waiting on jobs 261117-223709-25c2a0 261117-223712-5a12b1 261117-223717-5549cc 261117-224058-305d59 261117-224124-3ca3bb 261117-224128-ea8ae6
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:9b218a0 (branch pushed): driver.h erases a closed group in pump() after its
step (ready list unlinked if still queued), open() loses the closed-row path, settle() returns
after fail_all closes the group (it read the freed Worker), `groups()` counts rows. Tests in
test/driver_close_test.cc. The first three jobs (at 9b218a0) build the test by hand; the suite job 261117-224058-305d59 runs it through ctest at c06510a. Fails-without jobs (ASan): 261117-224124-3ca3bb drops settle()'s return, fenced_row must fail; 261117-224128-ea8ae6 drops pump()'s erase, bytes must fail.

## Next

1. Read the four jobs; fix what fails.
2. Read the two fails-without jobs.
3. Fill the Evidence table; ready for review.

## Blocked on

Nothing. The lead put CMakeLists.txt in my file set; the registration is committed (consensus:c06510a).

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
