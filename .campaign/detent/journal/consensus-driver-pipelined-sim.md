# consensus-driver-pipelined-sim: journal

State: waiting on jobs 261124-020911-4b0af3 261124-020914-c49b0b 261124-020918-cb3518 261124-020921-0e0ba5 261124-020925-4954e1 and the A3 verify
Quest: .campaign/detent/quests/consensus-driver-pipelined-sim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and docs done at consensus:551a5e6 (merged main 54c6599 clean). Jobs A1 (120 seeds default
mix; 60 seeds each forced at depth 4 and 16), A2 mutants (overlap, previous-term) and A3 verify
queued. Next after they finish: paste `campaign brief`'s table, push, set ready for review.

## Next

1. Read the jobs; if all pass, paste the Evidence table, fill Result, push, State "ready for review".

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

test/driver_simulation.cc: every seed's groups run at a Flow through `GroupOptions::scheduling.flow`:
seed % 4 picks {1}, {4, 16 KiB window, 2 KiB frames}, {16}, {4}; the second and third also set
follower batching (`follower_batch_entries/bytes` = rpc_entries/rpc_bytes) so a follower Worker's
merged-append path runs through a Driver. `SIM_DRIVER_PIPELINE=N` fixes every seed at depth N.
The summary prints a new last line: appends with entries sent, those sent with another in flight
to the same peer (same term, within a heartbeat), the most in flight, those delivered after a
later one, and seeds at depth above 1 with a second append in flight. Interface unchanged
(`seeds steps first`, `SIM_DRIVER_SEEDS`); docs/mechanisms/simulation.md describes it.

Probe at consensus:7118df2 (job 261124-014735-3e572f, 60 seeds): 52123 appends with entries, 493
sent with another in flight (at most 4), 48 delivered after a later one, 45 of 46 seeds at depth
above 1 had a second append in flight, every oracle held. Follower batching engaged: a probe
(261124-020539-7ba07b) counted about 600 held appends in 60 seeds, about 35 of them merged 2-4.

Mutants (follower append handling in core.h, runner `.scratch/pipelined/mutant.sh` in the
worktree, copy of include/ patched, simulation rebuilt, Driver seeds alone): `overlap` (an
already-held entry no longer skipped) fails at seed 1 ("conflict with committed log prefix");
`previous-term` (previous entry's term unchecked) fails at seed 2 (state machine safety). Both
also fail at forced depth 1 (261124-014439-1733ce, 261124-014442-e66bd8 at 22e6b15), so they are
not pipelining-specific. Mutants of the batching path alone (worker.h hold: contiguity
261124-014725-7daa04, term 261124-014727-9c0961, peer 261124-014729-8b0461; bounds
261124-015131-aa9eff; core.h `more` loop term 261124-014732-04eab3) all survive 60 seeds: Core
re-checks each merged append (previous, term) and the bounds are rarely reached, so they are
equivalent or too rare for this load. Not done: a mutant observable only at depth > 1.

## Rulings I made

- Flow per seed cycles (1, 4 with binding window and frame, 16, 4) with SIM_DRIVER_PIPELINE as
  the override, so the default run keeps its cost and covers every depth driver-pipeline names.
- Follower batching on for two of the three pipelined cycles, off for the third, so both paths run.
- In-flight counter is observed at the network (Sink Send and delivered AppendResponse), not in
  Core: leaders' state is not visible through the Driver; a lost answer ages out after a heartbeat.
- The mutant runner is in .scratch (not committed): tools/mutants.sh compiles the core simulation
  and cannot isolate the Driver seeds, and tools/ is outside this quest's file set.
