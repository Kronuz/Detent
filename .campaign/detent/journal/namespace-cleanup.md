# namespace-cleanup: journal

State: waiting on jobs 261118-054213-4d282a 261118-054246-bd4590 261118-054246-e71c4b 261118-054245-ae0ed2 261118-054252-f9e36d 261118-054253-6db3c4 261118-054255-14f77b 261118-054256-dbe2be 261118-054257-262af3 261118-054258-e74f8d
Quest: .campaign/detent/quests/namespace-cleanup.md (the lead's; never edit it)

## Doing

All three changes committed at ba9fede6 (A1 test + mutant case, A2 deletion, A3 runner conversion); jobs queued.

## Next

1. When jobs finish: A1 mutant 4d282a must pass (mutant table-unbounded killed by test_authority_history_power_cut); if the test does not trip on the table (survives), raise the tightness (the log has 4 namespaces; bound for 1 may be too wide) and rerun.
2. A3: a job that shows survived/no-verdict means that patch/target pair needs another choice (c4 with test_local_paged_definition and c3 with the bindings suites were guessed).
3. Push, report.

## Blocked on

Nothing. Note for the lead: A1's test lives in tests/detent_authority/test_history_power_cut.cc (beside `refused_seal`, which already checks the seal and block bounds), outside the quest's file set; test_namespace_history.cc is a cluster test with no History-with-limits harness.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | test: power_cut `single` Limits case (refuses at "history file layout"); mutant job 261118-054213-4d282a (`history_mutant.sh table-unbounded`, --fails-without); suite 261118-054246-bd4590 |
| A2 | `git grep` job 261118-054245-ae0ed2 (src include tests: no match); suite 261118-054246-bd4590 |
| A3 | --fails-without jobs: agent_store f9e36d, hash 6db3c4, repair 14f77b, one_knob dbe2be, dense 262af3, bindings e74f8d, history (A1's job); service/mirror/test_history runners already used the convention |
| A4 | native 261118-054246-bd4590, pins 261118-054246-e71c4b |

## Result

CompletionScheduler, CompletionPort, CoordinatorOptions::completion_port and test_completion_scheduler deleted (NativeDriver alias moved to coordinator.hh); live docs updated (DEPENDENCY_UPGRADES, INCREMENTAL_PUBLICATION_ARCHITECTURE, design/http-coroutines). Historical status docs and benchmarks/library-adoption-closure-20261019.json still name test_completion_scheduler (records of past runs, left).

## Rulings I made

1. Runner convention: exit the run's own nonzero status when killed, 0 when survived, 126 with no verdict (as service_mutant.sh).
2. history.cc needed no change: the bound check was right, only unprotected. The mutant is a new inline case `table-unbounded` in history_mutant.sh, not a patch file.
3. A3 jobs use one representative patch per runner; the plain runners' conversion is what is proven.
