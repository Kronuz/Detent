# consensus-faultfs: journal

State: waiting on jobs 261120-172913-b7370e 261120-173850-717ff9 261120-173853-926852 261120-173855-a141aa 261120-173858-656e1d 261120-173901-5f8751 261120-173913-d5f4fc 261120-173914-7a4d22 261120-173915-7bb349 261120-173917-e74484
Quest: .campaign/detent/quests/consensus-faultfs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs at consensus:5c25430 (pushed): suite b7370e. A2 --fails-without mutants: wave_open cut 717ff9
(worker.h store-fence lines removed), shared_worker 926852 and simulation 656e1d and shared_cluster
5f8751 (driver_mutants.py ack-before-turn-barrier), driver a141aa (checkpoint-drops-suffix, case
checkpoint_keeps_acknowledged). Same mutants at main 8801b99 for equivalence (no --for; read the
exit): d5f4fc, 7a4d22, 7bb349, e74484. A job that printed BUILD FAILED or crashed is not evidence:
read its output for `reached the test`.

Eight in-set test files converted at consensus:5c25430 (faultlab pinned a3b71d1 as journal does;
Host dir `host` via a per-file host_io(); cut = power_off_after(cut - 1); power_loss() =
crash(seed, size_pick::sector)). Suite job queued. A1 is not met: driver_close_test.cc and
worker_test.cc (batch_turn_room) still use memory_io and are outside the quest's file set.

## Next

1. Read the suite result; fix compile or behavior differences.
2. A2 mutant jobs (--fails-without), one per crash-model test: wave_open cut (drop the two
   `storage fenced outside worker` lines in worker.h), shared_worker and simulation
   (driver_mutants.py ack-before-turn-barrier), driver (checkpoint-drops-suffix, case
   checkpoint_keeps_acknowledged), shared_cluster if a mutant reaches it.
3. When the lead adds driver_close_test.cc and worker_test.cc to the file set: convert them
   (driver_close_test has FlakyIO over MemoryIO and close_crash), then rerun everything.

## Blocked on

Lead: A1 (`git grep memory_io` finds nothing) needs test/driver_close_test.cc and test/worker_test.cc
in the file set. Option (a) add both (recommended: worker_test is one line; driver_close_test
has FlakyIO and close_crash over MemoryIO, which faultfs's add_fault sync_directory covers);
option (b) narrow A1 to the eight files.

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
