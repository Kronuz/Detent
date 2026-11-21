# consensus-faultfs: journal

State: waiting on jobs 9023a5 ec18f5 30d478 4facd6 30a415 d3c8ff 54ff3d b4ddf9 (branch, A2) and 3e84b5 2e491d 17bc5d 68dcbc 651de8 (main baselines), CI at e4e82ad
Quest: .campaign/detent/quests/consensus-faultfs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Lead's A2 review of the 813d95f mutants (ids 261120-17xxxx):
1. close_crash lost the loop-throw mutant (805b3a survived; main 38db6e killed it). Cause: MemoryIO's
   cut threw PowerLoss (no std::exception), escaping step() and ending the Driver's loop; faultfs
   fails with EIO (system_error), which step() catches, so the loop never threw. Fix
   consensus:e4e82ad: CutIO wraps close_crash's host directory and turns a power-off failure into
   PowerCut (no std::exception). Local: unmutated 8130 checks 0 failures; mutant fails at seed 1
   cut 53, as main. Not the dropped failed_syncs checks (those are failed_close/second_driver).
2. wave_open cut's mutant never ran (quoting). Mutants now in a script,
   ~/.scratch/detent/consensus-faultfs/mutants/mutate.py (its bytes are the jobs' identity).
3. shared_worker, simulation and close_crash's frontier mutant survive on main too: not evidence.
   Candidates now: sends-before-durability (tools/mutants.sh's) and checkpoint-drops-suffix for
   shared_worker and the driver simulation; close_crash's evidence is the loop-throw mutant.

Jobs at e4e82ad (--for A2 --fails-without), ids 261120-1912xx: close_crash loop-throw 9023a5;
wave_open store-fence-unnamed ec18f5; shared_worker sends 30d478, checkpoint 4facd6; simulation
sends 30a415, checkpoint d3c8ff; driver checkpoint-drops-suffix 54ff3d; shared_cluster
ack-before-turn-barrier b4ddf9. Main 8801b99 baselines, no --for, ids 261120-1907xx: wave_open
3e84b5; shared_worker sends 2e491d, checkpoint 17bc5d; simulation sends 68dcbc, checkpoint 651de8;
close_crash loop-throw is 38db6e (killed). A mutant counts only where main's baseline is killed too.

## Next

1. Read the jobs; for each test keep a mutant killed at both main and e4e82ad; replace a
   surviving one (new script file, not an edit of mutate.py).
2. A1/A3: `ci:macos-arm64` at the head, ctest time <= 160.8 s (1.5 x main's 107.19 s, main run
   44829606479); grep memory_io empty.
3. Fill Evidence, write Result, set ready for review.

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

- driver_close_test's FlakyIO is `disk.add_fault({op::sync_directory})` (fires on the next
  directory sync, once); its `failed_syncs == 1` checks are dropped: close_in_doubt refusals and
  `parked` prove the failure. close_crash's end-of-run test (old Disk::dead) is
  `mutations() - base >= cut`. created_bytes's simulated disk part is filesystem::used().
- Helpers (host_io, sector_cut) are per file, as journal does; no shared header (file set).
- shared_cluster: a node's disk crash seed is the cluster's random(), not a per-disk stream.
- driver_simulation: held IO fails with system_error(EIO) where it threw PowerLoss.
- close_crash runs on CutIO, a faultfs directory whose power-off failures throw PowerCut (no
  std::exception), as MemoryIO's PowerLoss did: the case checks callers complete when a throw ends
  the Driver's loop, which only an exception step() does not catch can show.
