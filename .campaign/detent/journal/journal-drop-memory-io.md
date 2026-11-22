# journal-drop-memory-io: journal

State: waiting on jobs 261121-222332-05d7c1 261121-222339-b9f595; then quest 1's landing (consensus-install-test-faultfs)
Quest: .campaign/detent/quests/journal-drop-memory-io.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Pushed journal:9f5e846 (header deleted, bench on faultfs, mentions dropped). Waiting on the suite and bench jobs and CI.

## Next

1. When the jobs and CI are green, write "ready for review": only quest 1's landing remains. After it lands, check `git grep memory_io` in consensus main is empty.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `git grep memory_io` in journal at 9f5e846: empty. Suite: 261121-222332-05d7c1. Bench builds and runs: 261121-222339-b9f595. consensus: after consensus-install-test-faultfs lands (its branch's `git grep memory_io` is empty). CI on 9f5e846 pending. |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

bench/group_io_bench.cc is outside the quest's file set; the assignment named it. group_io_bench times Host::group_io's table over an in-memory directory with no real files, so it moves to faultfs, not PosixIO. It was not in CMakeLists; it is now the journal_group_io_bench target (linked to faultlab) so the suite compiles it. docs/perf/group-io.md says faultfs for MemoryIO. The recorded numbers there were taken on MemoryIO and are not re-measured.
