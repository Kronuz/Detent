# http-affinity-death-tsan: journal

State: waiting on jobs 261119-144222-6534ff
Quest: .campaign/detent/quests/http-affinity-death-tsan.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 job 261119-144222-6534ff (http:ec273dd, TSan RelWithDebInfo, coroutine_test x20, prints the first two TSan reports). Hypothesis: RelWithDebInfo defines NDEBUG, so the death test's NDEBUG branch performs the deliberate off-thread writer/BodyStream touch for real, and TSan reports it as a race (exit 66 before the reply). To confirm from the report.

## Next

1. Read the A1 report; name the race.
2. Fix in test/coroutine_test.cc (skip the NDEBUG branch under TSan, or the real cause if the report differs); push.
3. A2 loop job x20 at the fix.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

<Decisions inside the quest's scope, so the reviewer can check them.>
