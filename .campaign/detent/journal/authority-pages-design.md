# authority-pages-design: journal

State: waiting on jobs 261122-210412-8d650b 261122-210428-976f07 261122-210429-1ed7a8 261122-210431-3080d5 261122-210432-93640e 261122-210434-c73864
Quest: .campaign/detent/quests/authority-pages-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness at bench/authority_pages/ (authority_pages_bench.cc, build.sh, run.sh, check.py): seals mode (per-seal
build cost with the relay's build_generation, relocation rule, writes+F_FULLFSYNC, LtHash-shaped records
digest) and commit mode (durable single-voter Node with positions and history; arms none, thread, inline,
write interleaved on one node).

Round 1 (smoke 261121-214353-49cebb at 83402862) failed in its build: page_window.hh needs src/detent_local;
its five rows (261121-214754-909eb7, -214758-617a27, -214802-132bf5, -214806-067a3a, -214810-64032c) were
skipped. Fixed at 449eed53 (link detent::relay_page_window), then a local smoke found two run failures:
the journal bootstraps only into an empty dir (history moved beside it, 36ae64aa) and same-key same-value
producers in one group gave NoChange (distinct values, bf55a099). Local smoke passes at bf55a099.
Round 2 at bf55a099: smoke 261122-210412-8d650b (light), rows exclusive after it:
- 261122-210428-976f07 seals 100K (4 x 25,000), 30 seals of 2,048, written and synced
- 261122-210429-1ed7a8 seals 5M (10 x 500,000), 30 seals of 2,048, written and synced
- 261122-210431-3080d5 seals 5M, 30 idle seals of 16
- 261122-210432-93640e commit 100K, none/thread/inline/write x3
- 261122-210434-c73864 commit 5M, none/thread/inline/write x3

## Next

1. Smoke passes, then rows (exclusive, <=10 min each): seals 100K (4x25,000) and 5M (10x500,000) at 2,048
   changes and at 16 (idle seals); commit 100K and 5M, 3 rounds of the four arms.
2. Write docs/design/authority-pages.md from the rows; State "ready for review".

## Blocked on

Nothing.

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

1. rc3 write load = SCALE_TARGETS.md:117's proposed baseline (2,000 tx/s sustained, 10,000/s burst), one change
   per transaction, uniform keys; with seal_transactions 2,048 (state.hh:28) that is about 1 seal/s and 5/s.
   The commit arm runs this host's closed-loop maximum (64 producers), since one voter here commits below it.
2. Corpora are relay_window_bench's synthetic namespaces: 100K = 4 x 25,000, 5M = 10 x 500,000 (a history
   file holds at most 500,000 changes). The dense-pages r12 corpora store definitions in an older encoding the
   C++ builder refuses (docs/mechanisms/paged-transfer.md).
3. The build is the relay's own (build_generation), fed a W6 records body from an in-memory history; the
   records encode is timed apart (encode_ms), the decode is inside the build and counted as build cost.
4. Commit mode's authority holds its own 100,000 keys for both builder corpora: the default state bound
   (~133 MB, test_history_large_base.cc:138) does not fit 5M, and the arms measure the builder's effect.
