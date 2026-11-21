# consensus-faultfs: journal

State: waiting on jobs 805b3a a0cfb7 eb9284 94af61 8e01d0 0ea5f5 845289 and CI run 44852156981 (macos-arm64 queued) at 813d95f
Quest: .campaign/detent/quests/consensus-faultfs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

All ten test files converted at consensus:813d95f (pushed; no memory_io left). Lead added
driver_close_test.cc and worker_test.cc to the file set. Jobs at 813d95f, ids 261120-17xxxx:
suite f2c63e (--for A1,A3, 174557); A2 --fails-without: wave_open cut eb9284 (worker.h store-fence
lines removed), shared_worker 94af61, simulation 0ea5f5, shared_cluster 845289 (driver_mutants.py
ack-before-turn-barrier), driver 8e01d0 (checkpoint-drops-suffix), close_crash 805b3a (driver.h
loop catch removed) and a0cfb7 (worker.h close_store frontier guard removed). Same mutants at main
8801b99, no --for, read the exit: d5f4fc 7a4d22 7bb349 e74484 (the first four) and 38db6e d5d909.
A mutant job that printed BUILD FAILED, or never printed `reached the test`, is not evidence; a
mutant that survives on faultfs means drop it or find another. Local: -fsyntax-only of every
converted test compiles (peers_test not checked).

## Next

0. D57/msg-178: the suite check is CI's `macos-arm64` job; local suite jobs cancelled. A1 and A3
   cite `ci:macos-arm64` at the head, plus the grep for A1 (empty at 813d95f). A3's bound: that
   job's `Total Test time (real)` at the head <= 1.5 x main's 107.19 s (main run 44829606479,
   job 136616496277; so <= 160.8 s). At 813d95f, other jobs of run 44852156981 passed (freebsd,
   asan, tsan, linux gcc/clang/arm64); macos-arm64 and macos-x86_64 were queued.
1. Read the suite (time within 1.5x of main's: need main's suite time) and mutant results; push
   triggers CI, read it.
2. Fix differences; fill the Evidence table; write Result; set ready for review.

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
