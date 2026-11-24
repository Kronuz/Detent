# split-journal: journal

State: working
Quest: .campaign/detent/quests/split-journal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Verdict table below (A1) written before any move. Code worktree: /Users/kronuz/code/Kronuz/journal-wt/detent/split-journal, base fc5c59b (journal). Local build: ~/.scratch/journal-split/build; base `ctest -N` test lines in ~/.scratch/journal-split/base-ctest.txt (106 tests).

Verdicts (files over 1,000 lines; `git ls-files | xargs wc -l`):

| File | Lines | Verdict |
|---|---|---|
| test/journal_test.cc | 2376 | split: four topics in one file: the Journal on a memory IO (append, recovery, corruption), checkpoints and artifacts, Store, and the reopen/park cases on a Host with the heap counter. Seams at the test-function boundaries; shared harness (check, MemoryIO model) to a support header; main and the parameterized sweeps stay with the heap hooks |
| test/host_test.cc | 1424 | split: Host lifecycle, views and handoff (lines 1-922) stay; the idle-group cases (own `namespace idle` model, 12 cases, 923-1402, one design doc) move to host_idle_test.cc; main and its case list stay in host_test.cc, so the CMake case regex is unchanged |
| test/turn_seal_test.cc | 1180 | keep: one mechanism (TurnDriver::seal), 23 cases sharing Consumer, Model and the staging helpers; the `repeat*` family (about 300 lines) uses the same sealer API and helpers, so each half would be a fragment of the other |
| include/journal/turn_log.h | 1196 | candidate, not mine (include/**): turn_log_detail (114-496: Emitter, Sectors, RecordReader, manifests) is a recognizable piece to a header of its own; the TurnLog class body (498-1195, 700 lines) is a restructure (SPLITTING.md, large class), not a move |
| include/journal/host.h | 1068 | candidate, not mine (include/**): host_detail (95-319, shard and range codecs) could be its own header; Host (359-1067) is one 700-line class body, a restructure |

## Next

1. Split host_test (idle cases), prove with split_check, build, ctest -N.
2. Split journal_test, same.
3. docs/dev/splits.md (A1), verify, push.

## Blocked on

The two headers (include/**) are outside the file set; listed above for the lead.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
