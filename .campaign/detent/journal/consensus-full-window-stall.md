# consensus-full-window-stall: journal

State: working
Quest: .campaign/detent/quests/consensus-full-window-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1, root cause: a core-level prototype (3 Cores over throttled FIFO links, 32 x 64 KiB in
flight, scratch ~/.scratch/detent/consensus-full-window-stall/proto.cc) to see whether the stall
reproduces in Core alone or needs the Worker/Driver/transport.

## Next

1. Name the root cause at file:line from the prototype's trace.
2. Deterministic test (test/full_window_stall_test.cc) failing at main; fix; --fails-without job.
3. A3 netem bench in the Linux VM; A4 suite, TSan, CI.

## Blocked on

Nothing.

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
