# http-affinity-death-tsan: journal

State: waiting on jobs 261119-153143-556c30 261119-153146-efd6a1
Quest: .campaign/detent/quests/http-affinity-death-tsan.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix pushed (http:9b1ccdc). Waiting on A2 loop 261119-153143-556c30 and suite 261119-153146-efd6a1, then CI.

## Next

1. Fill Evidence from `campaign brief`; check CI green; set State ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | job 261119-144222-6534ff at http:ec273dd (TSan RelWithDebInfo, coroutine_test x20): 6 of 20 fail (exit 0, the loop counts), runs 3 reports of the same race; the job's exit is 0 by design of the probe |
| A2 | job 261119-153143-556c30 at http:9b1ccdc: 20 of 20 expected, exit nonzero on any failure; suite 261119-153146-efd6a1 |

The race (A1 report): the third death test (`/affine-read`). Under NDEBUG the assertion is compiled out, so the handler body that moved to the `away` scheduler's thread really reads its BodyStream: `channel_service<null_mutex>::start_receive_op` (writes the receive op on the away thread, T1) races with `asio_park::drop` from the BodyStream push completing on the loop thread (T2), http_asio.h:1020. A deliberate misuse, a race by construction; http itself is correct (the Debug build asserts before it).

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
