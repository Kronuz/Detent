# journal-fuzz-decoder-pin: journal

State: working
Quest: .campaign/detent/quests/journal-fuzz-decoder-pin.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

<The current step, precisely enough for a fresh contributor to continue it.>

## Next

1. <step>

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261121-154444-95ae40: mixed 21,467, segment 220,207, checkpoint 18,230 executions in 60 s each, no crash; `bogus` fails configure |
| A2 | `ci:macos-arm64`, CI green at journal:9cdaa34 |

## Result

journal:9cdaa34: `JOURNAL_FUZZ_DECODER` (segment, checkpoint, or empty for the first byte) pins `journal_fuzz`; written by the lead.

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
