# consensus-close-fenced: journal

State: waiting on jobs 261117-224628-54ea6f 261117-224633-e4c88c 261117-224638-f7ecbd 261117-224642-346496 261117-224646-fc0854 261117-224650-1be635
Quest: .campaign/detent/quests/consensus-close-fenced.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code at consensus:c34fd80 (pushed). The first test commit did not compile (start() lacked a task<T>
overload), so the jobs at 9b218a0/c06510a are void, including fails-without 261117-224124-3ca3bb
(it passed on a compile error). Resubmitted at c34fd80: plain A1-A3 54ea6f, ASan A4 e4c88c, TSan
A4 f7ecbd, suite A5 1be635, fails-without under ASan: settle() return 346496 (fenced_row must
fail), pump() erase fc0854 (bytes must fail). Check the mutant logs show a runtime failure, not
"BUILD FAILED".

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
