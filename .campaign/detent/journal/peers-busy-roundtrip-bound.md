# peers-busy-roundtrip-bound: journal

State: waiting on jobs 261119-144617-d7f473 261119-144622-bacf48
Quest: .campaign/detent/quests/peers-busy-roundtrip-bound.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1 (A1): consensus:7beb709 makes fetch-alongside-consensus print, per run, the longest gap
between scenario pumps (executor stall), a 1 ms-sleep watchdog thread's worst oversleep (host
stall), and the fetching phase's round-trip bucket counts. Probes (~/.scratch/peers-busy-roundtrip-bound/probe.sh:
6 workers x N runs beside 16 niced spinners; one line per run, passing too):
- d7f473 RelWithDebInfo x72 -> ~/.scratch/peers-busy-roundtrip-bound/rel-1
- bacf48 TSan x36 -> ~/.scratch/peers-busy-roundtrip-bound/tsan-1

## Next

1. Read the probes: does the slowest bucket track the pump gap / host stall (cause: host) or not (transport wait)?
2. Derive the bound: 64 ms plus the host stall measured in the phase (stated headroom), keeping a
   100 ms transport delay failing; mutant = writer pauses 100 ms before sending an append
   (refs/mutants/peers-busy-roundtrip-bound/append-delay-100ms), job --fails-without; the failing
   CHECK must be the bound's line.
3. Suite (consensus, --minutes 50) and CI.

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
