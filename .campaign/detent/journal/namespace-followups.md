# namespace-followups: journal

State: waiting on jobs 261116-213212-4cf92a 261116-213215-5d0339 261116-213218-af4bac 261116-213221-fa377b 261116-213225-1c4d20 261116-213228-f34142 261116-213231-3269f9 261116-213235-f29ded 261116-213241-2beb28 261116-213241-e3603b
Quest: .campaign/detent/quests/namespace-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

All four fixes committed at 9cdbff4c (branch head); jobs submitted, none read yet. Code unbuilt: expect first-build compile or test failures (esp. the Growth log reaching a count cut in 40 seals, the tight-limits seal in test_history_power_cut.cc refusing with "above its kind's bound", and the RLIMIT_FSIZE staging test).

## Next

1. Read the jobs (`campaign brief`); fix any red, rerun.
2. A mutant that survives gets a stronger test (seal_unbounded may survive: the table or block bound alone may be what the tight limits trip).
3. Fill Evidence and Result from `campaign brief`, set "ready for review".

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
