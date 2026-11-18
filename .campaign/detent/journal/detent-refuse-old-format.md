# detent-refuse-old-format: journal

State: waiting on jobs 261118-091843-ea45c3 261118-092815-15d102 261118-092816-620656 261118-092817-880e5a 261118-092818-bf9ff0 261118-092819-c7250d
Quest: .campaign/detent/quests/detent-refuse-old-format.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Lead's ruling (msg: unstamped = unknown, decide by content) done at Detent:10ca3544. Jobs, all at
that head unless noted: old-fixture build at 53abd637^ 261118-091843-ea45c3 (queued behind a
strict hold since 09:18); test at the fix 261118-092815-15d102; same test at the mutant
(service.cc reverted, ref refs/mutants/refuse-old-format, Detent:3716cf13) 261118-092816-620656;
new detentd on the old-built journal 261118-092817-880e5a (after the fixture job); new detentd on
the current-format unstamped trials 10000-main and 5000000-flat-r1 261118-092818-bf9ff0; suite
261118-092819-c7250d.

## Next

1. When 261118-091843-ea45c3 finishes, embed its small journal (`.scratch/old-fixture/data`, a few
   KB) in test_format_refusal.cc as the real format 1 case, unstamped and stamped 1 (the test
   today crafts the unstamped case with an unreadable manifest, and a stamp of 1).
2. Fill the Evidence table from `campaign brief`; check CI on the branch; set ready for review.

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

- Cause (found by reading, not by the 10K fixture, whose first job is still queued): a failure before
  admission is caught by Service::run's inner handler, which never settled `ready`, so the
  constructor (detentd) hangs. The recovery fence is one such failure. Fixed there.
- Superseded by the lead's ruling: no stamp = unknown. An unstamped directory with a journal is
  replayed: failure before admission = format 1 (refused, naming the failure); success = format 2,
  stamped before admission. A stamp of 1, 3 or garbage is refused at once.
- (Superseded, kept for the record) 53abd637 is not the only layout change since 2026-10-23 (711a114e's value encoding changed under
  unchanged magics), so no magic names the old format. Ruling: a storage-format stamp
  (`<dir>.history/format`, `storage_format = 2` in service.cc); an existing journal without one is
  format 1. history.cc is unchanged: its tables are covered by the stamp and a magic check there
  would be a second way to do one thing.
- history.cc is unchanged: the content check is the replay itself (image magic, entry apply, history
  restore all run in it), so no second check of old bytes there.
- CMake registration is in the file set now (lead).
