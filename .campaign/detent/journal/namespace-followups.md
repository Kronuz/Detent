# namespace-followups: journal

State: waiting on jobs 261117-005923-b9915c 261117-005929-a909be 261117-005901-b41eac 261117-005901-990548 261117-005901-f7ee1a
Quest: .campaign/detent/quests/namespace-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 (head 9cdbff4c): suite 261116-213212-4cf92a, A3 261116-213225-1c4d20 and pins passed; mutant seal_unbounded 261116-213218-af4bac killed. Causes found and fixed at dfef6d59:
- the mutant runners exited 0 on a kill, which `--fails-without` reads as a pass of the wrong sign: runners now return the test's own nonzero status (126 for no verdict). The no_bounds, no_count_cut (killed by the count_cuts assert) and stage-poisons-adopt (killed) runs were already kills.
- A4 mirror_tests.sh hit ctest's 900 s timeout on test_relay_mirror_crash under load (passed in 168 s in the native gate): timeout 3000.
- native 261116-213241-2beb28: only test_authority_snapshot_catchup failed (a read timeout in test_multivoter.py during 100 transactions). Checking whether it is mine (a history bound stall) or a flake: the same test at my head 261117-003147-0c5227 and at main 261117-003152-59ad5a.

## Next

00. Round 2 (head dfef6d59): A4 mirror_tests 003209-5a78d9 passed, both A4 mutants killed, pins passed, snapshot_catchup passed at my head (known flake). Native 003229-8c7477 also failed test_delivery_repair_scrub-worker: `scripted invariant at line 499` of tests/detent_delivery/test_repair.cc (scrub-carry: a pass hashed 2561074 bytes after the delta against a new root of 2752016, bound 200000), a coordinator hashing-count test that never touches the relay host. Attribution: `ctest --repeat until-fail:8` at my head 261117-005923-b9915c and at main 261117-005929-a909be. History mutants resubmitted by the lead as 261117-005901-b41eac, -990548, -f7ee1a (queue fault).

0. Lead (msg): test_authority_snapshot_catchup is a known flake that ci-test-flakes fixes on its branch (Detent:55b6474a: a transport timeout retries with the same sequence). Debug it only if my head fails it and main passes it more than once.

1. Read the jobs; if snapshot_catchup fails at my head and passes at main, the bound check stalls history in that run: debug (journal `history failed` log, bounds vs the test's policy) and fix. If it also fails at main, report it as pre-existing.
2. Fill Evidence from `campaign brief`; set ready for review.

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

- A1: History::seal checks the seal file, each output (block or base by its Output) and the table against `file_bounds(limits, seal policy, previous policy)` before queuing; a miss throws length_error and stalls history as any seal failure does (ready_ false). The previous policy is the layout's table policy before the seal, or the seal's own for the first.
- A3: dropped the relay's CompletionScheduler too (built only to feed those fields; the coordinator's completion_port was only checked, never run through). The queue and driver stay, prepare_coordinator needs the driver. docs/dev/docs-site-audit.md names `disk_*` but is outside the file set; left.
- A4: staging failures in adopt and in a read repair remove their temporaries and rethrow; the store is poisoned only from the first link (adopt) or the rename (repair). Test injects EFBIG with RLIMIT_FSIZE (no root needed).
- Mutant runners and the new test scripts live in tests/detent_authority/test_history_mutant.sh and tests/detent_relay/{mirror_tests,relay_health_check}.sh, since history_mutant.sh is outside the file set.
