# namespace-cleanup: journal

State: waiting on jobs 261118-063425-3f932a 261118-063427-d1c59a 261118-063429-e22141 261118-063431-4826a4 261118-063434-d5b163 261118-063436-6945df 261118-063436-d7652a
Quest: .campaign/detent/quests/namespace-cleanup.md (the lead's; never edit it)

## Doing

msg (lead): 4d282a was not a valid kill: it died of `std::invalid_argument: seal namespace count` (a limit of one namespace refuses the seal decode before the table check), the same failure the unmutated test would hit. Fixed at 4d654623: a `Sprawl` log (60 namespaces of 100 characters, one changed per seal, a table at every seal) under `namespaces = 1`, so only the table outgrows its bound. A3's hash, repair and one_knob jobs had dead patch/target pairs (s4-a3 no longer applies; c4 has no killing test); resubmitted with s4-f1/test_delivery_hash_stats_tear, s3b-m3/test_local_paged_snapshot_plan and c1/test_local_paged_definition.

## Next

1. Read 3f932a's log: it must end in an assertion on `refused.find("above its kind's bound")`/`starts_with("history file layout ")` (an abort at the assert, not an exception); quote that line here. d5b163 is the plain run and must pass; if either fails, adjust Sprawl (names, count) and rerun.
2. A3: 3f932a, d1c59a, e22141 and 4826a4 must each show MUTANT KILLED with a nonzero exit; the already-killed runners are agent_store f9e36d, dense 262af3, bindings e74f8d.
3. Push, report.

## Blocked on

Nothing. (A1's test lives in tests/detent_authority/test_history_power_cut.cc, beside `refused_seal`; now in the file set.)

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | test: power_cut `Sprawl` under `namespaces = 1` (refuses at "history file layout"); mutant job 261118-063425-3f932a (`history_mutant.sh table-unbounded`, --fails-without; 4d282a is void, see Doing); plain run d5b163; suite 261118-063436-6945df |
| A2 | `git grep` job 261118-054245-ae0ed2 (src include tests: no match); suite 261118-063436-6945df |
| A3 | --fails-without jobs: agent_store 261118-054252-f9e36d, dense 262af3, bindings e74f8d (killed); hash 063427-d1c59a, repair 063429-e22141, one_knob 063431-4826a4, history 063425-3f932a (pending); service/mirror/test_history runners already used the convention. 6db3c4, 14f77b, dbe2be and 37fe45 are void |
| A4 | native 261118-063436-6945df, pins 261118-063436-d7652a |

## Result

CompletionScheduler, CompletionPort, CoordinatorOptions::completion_port and test_completion_scheduler deleted (NativeDriver alias moved to coordinator.hh); live docs updated (DEPENDENCY_UPGRADES, INCREMENTAL_PUBLICATION_ARCHITECTURE, design/http-coroutines). Historical status docs and benchmarks/library-adoption-closure-20261019.json still name test_completion_scheduler (records of past runs, left).

## Rulings I made

1. Runner convention: exit the run's own nonzero status when killed, 0 when survived, 126 with no verdict (as service_mutant.sh).
2. history.cc needed no change: the bound check was right, only unprotected. The mutant is a new inline case `table-unbounded` in history_mutant.sh, not a patch file.
3. A3 jobs use one representative patch per runner; the plain runners' conversion is what is proven.
