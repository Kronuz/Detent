# consensus-driver-flow review: correctness

Commit: consensus:f7a44f3a816fec39b853f0131a8e8a2a41d33283
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1: 261123-063949-57aa77 (all of driver_test at 10eb12e, 0 failures) and 261123-105921-3833cd
  (`flow_reaches_core` at f7a44f3, 830 checks, 0 failures). The merge f7a44f3 brings only
  CMakeLists.txt, test/driver_mutants.py, test/transport/groups_test.cc and tools/mutants.sh
  from main (`git diff 10eb12e f7a44f3 --stat`), none of the quest's files.
- A2: mutants d4fb075 (create builds `Flow{}`) and 1805289 (recover builds `Flow{}`), each
  f7a44f3 plus that one worker.h line (`git diff f7a44f3 <mutant>`): jobs 261123-105043-139c3a and
  261123-105047-a9361e fail with "pipelined on create/recover: 4 appends in flight to a silent
  peer, not 1" and "the largest 1048576" against the 262144 cap. The default case still passes
  under both, as it must.
- A4: CI 45229726672 at f7a44f3, every job green (tsan, asan-ubsan, freebsd, linux gcc/clang/arm64,
  macos arm64/x86_64).

## What I checked myself

Risk 1 (Flow reaches every Core; default unchanged). Every `Core` in `include/` is built in
worker.h, at 151 (create), 179 and 180 (recover, both branches) and 712 (the validation Core), and
all four pass `scheduling_.flow`. `scheduling_` is copied from the `scheduling` argument in all
three Worker constructors (worker.h:104,111,121) before `validate_configuration`, and `Driver::open`
passes `options.scheduling` whole (driver.h:198). `WorkerLimits::flow{}` is `Flow{}` =
`{pipeline 1, window 4 MiB, frame 0}` (types.h:126-130), the value the four sites passed before;
it is the struct's last member, so positional `WorkerLimits{...}` initializers elsewhere
(driver_simulation.cc:512) keep their meaning.

Risk 2 (validation). Core refuses `pipeline == 0` or `window_bytes == 0` with
`std::invalid_argument` (core.h:129), and the validation Core now carries the Flow, so `open`
throws before the group is registered (driver.h:197-199); the test checks `Flow{0}`. A
`frame_bytes` above `rpc_bytes` is clamped: the batch bound is the minimum of the node's, the
group's, the frame and the paced frame (core.h:1422). A `frame_bytes` below one command still
sends that entry alone (core.h:1427), and a window below one frame still allows the first flight
(core.h:1405), so no Flow value stalls a peer or sends a frame above `rpc_bytes`.

Risk 3 (A1 by value). `flow_reaches_core` checks the silent-peer flight count equals
`flow.pipeline` exactly (4, and 1 by default), the drained frames are at most 256 KiB and within
one entry of it, the default's frames are above 256 KiB and at most `rpc_bytes`, and the backlog
commits, on create and on recover. A Flow that reaches Core but is ignored would fail it: ignoring
the pipeline gives 1 flight, ignoring the frame gives 1 MiB frames, which is what the two mutants
print. `window_bytes` is not checked (F2).

Risk 4 (RPC lane, simulation oracles). The change touches no transport code. The RPC lane
(docs/mechanisms/rpc-lane.md) carries `DTRP` call frames on Bulk outside every group's credit; Raft
appends are group data frames under the per-group 4 MiB credit and 8 MiB queue
(host-transport.md:146,170), so the lane's limits do not apply to them (F3 for the queue).
`consensus_simulation`'s Core seeds never set a Flow and the Driver seeds use the default
(driver_simulation.cc:512), so CI runs them unchanged. Probes with every Driver group pipelined:

| Job | Flow | Result |
|---|---|---|
| 261123-114136-6d74f6 | `Flow{4, 16 << 10, 2048}` (below the sim's 4 KiB `rpc_bytes`) | 60 driver seeds x 3000 steps + regression seed: ok |
| 261123-114139-580b1d | `Flow{4, 4 << 20, 256 << 10}` (Xapiand's, frame above `rpc_bytes`) | the same: ok |

Script: `.scratch/detent/review-driver-flow/sim_flow.sh` (env `FLOW`, `SEEDS`), which patches
driver_simulation.cc:512 in the job's checkout. Jobs 261123-110526-50448e and
261123-110528-1ac61e failed to compile from a quoting bug in the first version of the script, not
from the code.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | The Driver simulation's oracles hold with a pipelined Flow on every group, but both probes print identical counters (4228 elections, 18370 proposals, 1286 checkpoints), so the sim's load likely never opens a second flight: followers' Workers receiving pipelined appends through a Driver (async storage, `follower_batch_*`) are exercised only by Core's pipeline_test, not end to end. Not a criterion here; it matters once strata sets Flow{4, ...}. | 261123-114136-6d74f6, 261123-114139-580b1d; driver_simulation.cc:512 | Before or with the strata change: a driver_simulation seed range with a pipelined Flow and a load that backs up (or a flights counter in its summary). |
| F2 | no | gap | A1 never reaches the window: 4 x 256 KiB is 1 MiB against 4 MiB, so a path that dropped only `window_bytes` would pass. Core's window is pipeline_test's. | test/driver_test.cc `flow_case` | Optional: a case with `window_bytes` below `pipeline x frame_bytes`. |
| F3 | no | nit | Nothing bounds a Flow against the transport: `pipeline x frame_bytes` above a group's 8 MiB per-connection data queue (host-transport.md:170) is dropped and counted, then resent on timeout. Xapiand's Flow (1 MiB in flight) is far inside it. | core.h:1404-1405; host-transport.md:146,170 | A line in DRIVER.md's Replication flow, or leave it. |
| F4 | no | nit | append-pacing.md:36-37 inserts "now sets it" into a section that describes the code at consensus:8801b99, and the line runs past the doc's wrap. | docs/mechanisms/append-pacing.md:36-37 | Drop the parenthetical; DRIVER.md already names the option. |

## Not run

- No sanitizer or other-platform runs of my own; CI 45229726672 covers them at this commit.
- The probes did not measure whether a second flight ever opened in the simulation (F1).
- No throughput measurement of Flow{4, 4 MiB, 256 KiB}; the CHANGELOG's 16 vs 64 MB/s is
  arithmetic from the round trip, not a job.
- Main has moved two files (docs/mechanisms/rpc-lane.md, test/transport/rpc_host_test.cc) since
  f7a44f3; neither is in this quest's files, and the merge before landing is the lead's.
- The session title: I ran as a subagent, not a titled session.
