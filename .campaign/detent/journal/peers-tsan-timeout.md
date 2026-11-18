# peers-tsan-timeout: journal

State: waiting on jobs 261118-125526-323c20 261118-125534-d50a24 261118-125551-0df91a
Quest: .campaign/detent/quests/peers-tsan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1, reproduce on macOS TSan (RelWithDebInfo, -fsanitize=thread, target
consensus_transport_peers_test in .scratch/build-tsan of the quest slot) at consensus:42019eb.
Runner: ~/.scratch/peers-tsan-timeout/loop.sh BUILD N LIMIT OUT [CASE] runs the binary N times
through a pty (line-buffered, so in-case printfs survive a kill), samples a run past LIMIT s
(`sample`, stacks in OUT/sample-i.txt), kills it, and writes OUT/summary.txt with per-case
max/mean. Exits 0 unless STRICT=1, so chained diagnostic jobs run even after a hang.
- 261118-125526-323c20: full x10, limit 75 s -> ~/.scratch/peers-tsan-timeout/main-full-a
- 261118-125534-d50a24: full x10 (after a) -> main-full-b
- 261118-125551-0df91a: refused-first alone x50, limit 30 s (after b) -> main-refused

CI facts (run 44379638851): stdout is a pipe, so a case's own printf stays buffered until run()
flushes it; the kill at 30.01 s lost whatever refused-first printed. No CHECK fired (stderr is
unbuffered and empty), so the run neither failed a check nor ended: refused-first or a later case
ran past ~21.7 s.

## Next

1. Read the three jobs' summaries: hang (samples) vs slow (per-case maxima).
2. Fix (hang) or set the timeout from measured times with 2x headroom (slow).

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
