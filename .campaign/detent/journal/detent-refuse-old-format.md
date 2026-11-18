# detent-refuse-old-format: journal

State: waiting on jobs 261118-091843-ea45c3 261118-092449-5660df 261118-092450-81b2de 261118-092511-a60fd9 261118-092512-7f51be
Quest: .campaign/detent/quests/detent-refuse-old-format.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix at Detent:af9a8479 (test-only commit Detent:3e687591 before it). Jobs queued behind a loaded
machine: old-fixture build 261118-091843-ea45c3 (53abd637^), test at fix 261118-092449-5660df,
test at the test-only commit (fails-without) 261118-092450-81b2de, new detentd on the old journal
261118-092511-a60fd9, suite 261118-092512-7f51be.

## Next

1. When the jobs finish: fill the Evidence table from `campaign brief`; if the real-fixture probe or the test fails, fix (service.cc) and rerun.
2. Check CI on the pushed branch (Docs was running at start); set State to ready for review.

## Blocked on

CMake is outside the file set: I added the 4-line registration of test_authority_format_refusal to
tests/detent_authority/CMakeLists.txt (commit Detent:3e687591) because the test cannot run without
it. Lead: add that path to the quest's files, or tell me to drop it.

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

- Cause (found by reading, not by the 10K fixture, whose first job is still queued): a failure before
  admission is caught by Service::run's inner handler, which never settled `ready`, so the
  constructor (detentd) hangs. The recovery fence is one such failure. Fixed there.
- 53abd637 is not the only layout change since 2026-10-23 (711a114e's value encoding changed under
  unchanged magics), so no magic names the old format. Ruling: a storage-format stamp
  (`<dir>.history/format`, `storage_format = 2` in service.cc); an existing journal without one is
  format 1. history.cc is unchanged: its tables are covered by the stamp and a magic check there
  would be a second way to do one thing.
- Consequence for the lead: every directory written before this lands (dense-sync-stack's 10000-main
  and 5000000-flat-r1, current format but unstamped) is refused and must be regenerated.
