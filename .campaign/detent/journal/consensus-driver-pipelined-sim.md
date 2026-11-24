# consensus-driver-pipelined-sim: journal

State: waiting on jobs 261124-052350-82cc5b 261124-052352-53da0a 261124-052354-fb8731 261124-052356-4538ad 261124-052358-086ed2 261124-052359-a0add1 and debug 261124-052456-811156 (fixing F1 of the correctness review)
Quest: .campaign/detent/quests/consensus-driver-pipelined-sim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 (review CHANGES at 551a5e6), consensus branch at 19c0727 (pushed):
- F1: the counter counts an append only when it extends an outstanding flight (previous == a
  flight's last entry), keeps the leader's matched mark, clears on rejection and on an installed
  image, and fails a seed whose chain exceeds its pipeline. At fe889c0/8500a7b the oracle fired:
  depth 1 seed 4 ("2 appends in flight ... at pipeline 1", job 261124-051721-39b95f / -052350-82cc5b),
  depth 4 seed 19 ("5 ... at pipeline 4", 261124-051724-a70d9e): the tracker still counts a flight the
  leader dropped. 19c0727 adds matched high-water and installs; seed 4 still fails
  (261124-052350-82cc5b). Debug job 261124-052456-811156 (`.scratch/pipelined/dbg.sh 4`) prints that
  group's sends and answers; read its log to find which Core path drops the flight. If the tracker
  cannot be made exact, count extension only on `previous == through` AND no answer/timeout seen
  and drop the failing oracle, saying so.
- ack-tip added to `.scratch/pipelined/mutant.sh` (jobs 261124-052358-086ed2 fails-without default,
  -052359-a0add1 at depth 1); docs updated.
- Still to do: A1 jobs above must pass; forced depth 1 job must print 0 extending appends; merge
  main; CI at the new head for A3; Evidence table; cite review notes F2-F6 in the Result.

## Next

1. Read 261124-052456-811156, fix the tracker, resubmit the A1/A2 jobs, push, wait for CI.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Failed jobs, named: 261124-021444-bceda5, -021450-081013, -021456-6f75bf (main arms, exit 127: my
`--commit` was passed inside the command; resubmitted below). 261124-025043-2cd267, -025050-f32e6e,
-025056-3f0bc4 were cancelled by a later `--quest` submit. No message (msg-N) is listed.

| Criterion | Evidence |
|---|---|
| A1 | 261124-014735-3e572f (60 seeds, 45 of 46 pipelined seeds had a second append in flight), 261124-020911-4b0af3 (120 seeds), 261124-020914-c49b0b (60 seeds at depth 4), 261124-020918-cb3518 (60 seeds at depth 16) |
| A2 | 261124-014735-3e572f; fails without: 261124-020921-0e0ba5 (`overlap`), 261124-020925-4954e1 (`previous-term`) |
| A3 | `ci:macos-x86_64`, `ci:ci` (CI run 45372452058 at consensus:551a5e6, green on rerun; attempt 1 failed only in groups fetch-alongside, filed as consensus-fetch-alongside-loop-stall) |

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
equivalent or too rare for this load. Not done: a mutant observable only at depth > 1; A2 as
written does not require one, and A1's counter (45 of 46 seeds with a second append in flight) is
what proves pipelining runs.

Runtime of the driver phase at CI's default (SIM_DRIVER_SEEDS=20, 3000 steps; strict, interleaved
pairs head/main, unoptimised `build`, one host): head 34.5, 29.9, 31.8 s (c4ea2e, 351c2c, f081eb)
against main 54c6599 28.1, 28.5, 26.9 s (24d3ad, 40fbca, 901a42): per-pair +6.4, +1.4, +4.9 s,
mean +4.3 s (27.8 to 32.1 s, +15%, pairs +5% to +23%). Not negligible for this phase, and the
cause is not isolated (more appends in flight and batching on 3 of 4 seeds, plus the counters).
Three earlier head runs under another load took 28.4, 28.6, 25.8 s (021447-93b7d7, -021453-191418,
-021459-13d657). ASan (RelWithDebInfo, the head, one run, beside 4 light holds): 23.6 s
(261124-021509-0302a0), not comparable to the unoptimised arms and with no main arm; CI's
consensus_simulation (59 s typical, 300 s TIMEOUT) will show the whole-test effect.

## Rulings I made

- Flow per seed cycles (1, 4 with binding window and frame, 16, 4) with SIM_DRIVER_PIPELINE as
  the override, so the default run keeps its cost and covers every depth driver-pipeline names.
- Follower batching on for two of the three pipelined cycles, off for the third, so both paths run.
- In-flight counter is observed at the network (Sink Send and delivered AppendResponse), not in
  Core: leaders' state is not visible through the Driver; a lost answer ages out after a heartbeat.
- The mutant runner is in .scratch (not committed): tools/mutants.sh compiles the core simulation
  and cannot isolate the Driver seeds, and tools/ is outside this quest's file set.
