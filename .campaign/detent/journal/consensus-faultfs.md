# consensus-faultfs: journal

State: ready for review
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

Lead msg (after e4e82ad): mutants committed as test/faultfs_mutants.py (consensus:a46f110; lead
widens the file set); shared_cluster baseline at main; on sends-before-durability, find why if no
branch seed kills it. Answer: the branch's 200-seed probe 9a4a70 kills it at seed 11 ("conflict
with committed log prefix", the same failure main's seed 399 shows); main over seeds 1-11 (258a4b)
kills it only at 399. Each crash model's stream reaches the fault at its own seeds; faultfs does
not miss it. consensus:bb961b3 adds {11, 3000} to driver_simulation's regressions (+0.1 s).

A2 at bb961b3 (--for A2 --fails-without, all reached the test, all exit 1), main 8801b99 baseline:

| Test | Mutant | main | bb961b3 |
|---|---|---|---|
| close_crash | loop-throw-suspends | 38db6e killed | 4f37ea (seed 1 cut 53) |
| wave_open cut | store-fence-unnamed | 3e84b5 killed | 7daa7f |
| shared_worker | checkpoint-drops-suffix | 17bc5d killed | 407361 |
| driver simulation | sends-before-durability | 68dcbc killed (seed 399) | fc047f (seed 11) |
| driver simulation | checkpoint-drops-suffix | 651de8 killed | 713715 |
| driver | checkpoint-drops-suffix | 7bb349 killed | 608dd4 |
| shared_cluster | ack-before-turn-barrier | e6bcf8 killed | ab67ca |

Dropped: shared_worker sends-before-durability (survives at main 2e491d and branch 30d478); the
frontier mutant and ack-before-turn-barrier on shared_worker/simulation (survive at main).
Main baselines use ~/.scratch/detent/consensus-faultfs/mutants/mutate.py (same bytes as
test/faultfs_mutants.py, which main lacks).

## Next

1. Review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `ci:macos-arm64` at bb961b3 (run 44860606419: 91 of 91 tests pass); 261120-214850-8b9fd8 (`git grep memory_io` finds nothing) |
| A2 | --fails-without at bb961b3, each killed at main 8801b99 too (Doing): close_crash 261120-192034-4f37ea, wave_open cut 261120-192035-7daa7f, shared_worker 261120-192036-407361, driver simulation 261120-192038-fc047f and 261120-192039-713715, driver 261120-192040-608dd4, shared_cluster 261120-192042-ab67ca; the same tests unmutated pass: 261120-214851-3501a7 |
| A3 | `ci:macos-arm64` at bb961b3: ctest 113.85 s, 1.06x main's 107.19 s (bound 160.8 s); every job of run 44860606419 green, ASan, TSan and FreeBSD included |

## Result

consensus:813d95f..bb961b3 (5 commits): the ten test files that built a journal on MemoryIO
(blocking_entry, driver_simulation, driver, driver_close, shared_cluster, shared_worker,
transport/peers, waiter, wave_open, worker) run on faultlab's faultfs (faultlab:a3b71d1); no file
includes journal/memory_io.h. Power cuts are `power_off_after` plus `crash(seed, sector_cut)`.
close_crash runs on CutIO (consensus:e4e82ad), which throws PowerCut at a power-off failure so a
throw can still end the Driver's loop; test/faultfs_mutants.py (consensus:a46f110) holds the A2
mutants; driver_simulation replays seed 11 (consensus:bb961b3). Suite time on macos-arm64: 113.85
s against main's 107.19 s (+6%). Left open: shared_cluster's mutant is the only one main never had
a pre-existing baseline for (now e6bcf8, killed); seed 399 no longer reproduces its old schedule
on faultfs and stays only as a record.

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
- driver_simulation keeps seed 399 and adds seed 11 to its regressions: 11 is where faultfs's
  crash reaches a leader sending before durability; 399 stays as the record of the old failure.
