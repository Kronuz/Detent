# consensus-driver-pipelined-sim: journal

State: ready for review (A3: CI run 45407143806 at consensus:8f21d10 was queued when written)
Quest: .campaign/detent/quests/consensus-driver-pipelined-sim.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing; fix round 1 done at consensus:8f21d10 (main already merged, pushed). Waiting only for CI.

## Next

1. A3 is CI run 45407143806 (and the suite verify) at 8f21d10.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Failed or cancelled jobs, named: 261124-021444-bceda5, -021450-081013, -021456-6f75bf (main arms,
exit 127, `--commit` inside the command); 261124-051721-39b95f, -052350-82cc5b, -051724-a70d9e,
-052352-53da0a, -052354-fb8731 (the chain-depth oracle fired on the Worker's resend-before-answer
order, see Result; fixed at 8f21d10); 261124-051728-97bb63 and others superseded by newer submits.
No message (msg-N) is listed.

| Criterion | Evidence |
|---|---|
| A1 | 261124-054533-5b8358 (default cycle, 60 seeds), 261124-054538-eaf00b (depth 4), 261124-054541-b8f48a (depth 16), 261124-054535-246acf (depth 1: 0 extending appends) |
| A2 | 261124-054533-5b8358; fails without: 261124-054544-8ae5fe (`ack-tip`, pipelining-specific; survives depth 1: 261124-054547-32f2aa), 261124-052358-086ed2 (`ack-tip` at 19c0727), 261124-020921-0e0ba5 (`overlap`), 261124-020925-4954e1 (`previous-term`) |
| A3 | `ci:ci`: CI run 45550615396 at 977b94e, green on every job (macos-x86_64 included) |

## Result

test/driver_simulation.cc: every seed's groups run at a Flow through `GroupOptions::scheduling.flow`:
seed % 4 picks {1}, {4, 16 KiB window, 2 KiB frames}, {16}, {4}; the second and third also set
follower batching (`follower_batch_entries/bytes` = rpc_entries/rpc_bytes) so a follower Worker's
merged-append path runs through a Driver. `SIM_DRIVER_PIPELINE=N` fixes every seed at depth N.
The summary prints a new last line: appends with entries sent, those sent with another in flight
to the same peer (same term, within a heartbeat), the most in flight, those delivered after a
later one, and seeds at depth above 1 with a second append in flight. Interface unchanged
(`seeds steps first`, `SIM_DRIVER_SEEDS`); docs/mechanisms/simulation.md describes it.

Counts at consensus:8f21d10, 60 seeds each (an append is pipelined when it extends a flight the
leader has not had answered; a resend or first flight is not): default cycle (5b8358) 189 of
52,123 appends with entries extend a flight, at most 4 in flight, 44 of the 46 seeds at depth above
1 had one; forced depth 4 (eaf00b) 238 of 53,043, at most 4, 58 of 61 seeds; forced depth 16
(b8f48a) 239 of 53,044, at most 4 (the load never opens more), 58 of 61; forced depth 1 (246acf) 0
of 50,961, at most 1, and the run fails any seed whose chain exceeds its pipeline. Every oracle
held. Follower batching engaged: a probe (261124-020539-7ba07b) counted about 600 held appends
in 60 seeds, about 35 merged 2-4.

Review F1 (the counter flagged 11 of 15 depth-1 seeds): not a Driver defect. At depth 1 the Core
never has a flight before a send (traced, jobs 261124-054420-bffb4a, -054210-f7768f, -054212-5397a0):
a leader's Worker handles a resend's timer before the answers already queued behind its storage, so
the Sink sees the resend after the answer that covers it, and the next append then extends it. The
tracker counts deliveries, so it now ignores a send whose last entry a delivered answer covers, keeps
the leader's matched mark, and clears flights on a rejection and on an installed image. The cost
in the system is one redundant resend per such race.

A2's pipelining-specific mutant is the reviewer's `ack-tip` (a success acknowledges through the
last flight, not the answered one): killed at the default cycle (054544-8ae5fe), survives depth 1.
Reviewer notes F2-F6 need no change: F2 (rejection-path mutants `reject-keep`, `reject-tip` survive
at every depth, benign to safety; a pipeline_test case is outside this file set), F3 (the five
batching mutants are equivalent, each Worker check has a Core counterpart), F4 (the runtime is the
pipelined seeds' own work), F5 (`SIM_DRIVER_PIPELINE=N` couples depth with batching and no window;
a separate `SIM_DRIVER_BATCHING` when driver-pipeline needs it), F6 (docs/mechanisms/simulation.md
is outside the file set; the lead widens it or accepts).

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
