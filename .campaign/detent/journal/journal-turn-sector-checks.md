# journal-turn-sector-checks: journal

State: working
Quest: .campaign/detent/quests/journal-turn-sector-checks.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1: RFTW0002 in `include/journal/turn_log.h` (journal worktree
journal-wt/detent/journal-turn-sector-checks): own sector layer in `turn_log_detail` (Stream,
Emitter writing whole sealed sectors from the kept partial sector, RecordReader verifying sectors,
`verdict`), `scan_segment` on sector verdicts, `recover` without in-block `later`; turn bound
scaled by 512/504.

## Next

1. Tests: turn_log_test.cc (format-agnostic: compiles at main too, for `--fails-without`),
   T9 rewritten on sector verdicts, turn_buffers/turn_seal reference emitters.
2. Fixtures regenerated (`journal_crc_fixture_test --generate`), fuzz target.
3. Doc turn-log-damage.md, CHANGELOG; jobs A1-A6.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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
