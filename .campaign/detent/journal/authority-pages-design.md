# authority-pages-design: journal

State: waiting on jobs 261123-102546-6e81cf 261123-102550-885bdd 261123-102553-3c089f 261123-102555-afdabb 261123-102558-6f96c6 261123-102602-d23ae1 261123-102605-a63417 261123-102608-7ae2b2 261123-102610-fdd616 261123-102613-0c4d8c 261123-102616-53d93e 261123-103432-c20472 261123-103435-b1e0ae 261123-103438-3b96b1
Quest: .campaign/detent/quests/authority-pages-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness at bench/authority_pages/ (authority_pages_bench.cc, build.sh, run.sh, check.py): seals mode (per-seal
build cost with the relay's build_generation, relocation rule, writes+F_FULLFSYNC, LtHash-shaped records
digest) and commit mode (durable single-voter Node with positions and history; arms none, thread, inline,
write run in the order named on one node).

Rounds 1-2 (smoke and build fixes, 449eed53, 36ae64aa, bf55a099): see git log. Round 2 rows: 100K seals
261122-210428-976f07 (exclusive), 5M seals 261122-210429-1ed7a8 (exclusive), 5M idle 261123-073248-5fdf61
(strict) passed and are in the doc. Commit rows 261123-073250-7ccbac (100K) and 261123-073252-43c224 (5M),
both strict at bf55a099, FAILED in round 6 with "transaction rejected".

Cause (bench bug, not an authority limit): seeding gives key k the value k; a group gave producer i the value
salt*64+i, so a pick of key k with salt*64+i == k is NoChange. Diagnostic at ff2d21ab (names status, key,
value, held) reproduced it locally on the same draw sequence (`commit 1 1000 160 none none inline none none
none`, light): "transaction 52 rejected: status 2 (key 53748, value 53748, held 53748)" = NoChange. Fix
07ac6b63: group values start past the seeded count. Proof jobs: 261123-102546-6e81cf at ff2d21ab
(--fails-without), 261123-102550-885bdd at the fix.

Split: a commit row is 4 strict jobs of one arm rotation each (none-thread-inline-write, thread-inline-
write-none, inline-write-none-thread, write-none-thread-inline: a Latin square, each arm once per position),
about 5-6 min each; `check.py combine LOG...` gives per-arm median group p50 over the jobs and the paired
delta against each job's own none run (median, min-max). 100K: 3c089f, 6f96c6, a63417, fdd616; 5M: afdabb,
d23ae1, 7ae2b2, 0c4d8c (all 261123-1025xx/1026xx). Plus 100K idle seals 261123-102616-53d93e (strict). A first submission (261123-102306 to -102331) was cancelled
unrun: zsh passed its options as one word, so they ran as the command.

The harness edit (commit-mode code only; the seals path is byte-identical in behaviour) makes the seal rows
"ran other code" at head, so they rerun at head, strict: 100K 261123-103432-c20472, 5M 261123-103435-b1e0ae,
5M idle 261123-103438-3b96b1. The doc keeps the exclusive bf55a099 numbers if the head reruns agree within
the strict noise (the strict 5M idle cold build read 2.2x the exclusive one); it cites both.

## Next

1. When the jobs land: `check.py combine` the 4 commit logs per corpus (~/.scratch/campaign/jobs/<id>/a1/log),
   fill the commit table, the 100K idle cell and the seal rows' head jobs; confirm the recommendation's
   builder-thread line (doc says *pending*); A1/A2 rows cite head jobs; State "ready for review".

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | docs/design/authority-pages.md: 100K priced from 261122-210428-976f07; 5M cells pending |
| A2 | 261122-210412-8d650b smoke passed; seals 261122-210428-976f07, 261122-210429-1ed7a8 (exclusive), 261123-073248-5fdf61 (strict) passed at bf55a099. Commit rows 261123-073250-7ccbac, 261123-073252-43c224 failed (bench NoChange bug, fixed 07ac6b63; proof 261123-102546-6e81cf fails-without, 261123-102550-885bdd); replaced by 8 rotation jobs 261123-1025xx/1026xx (see Doing). Round 2's 261122-210431-3080d5, -210432-93640e, -210434-c73864 (exclusive) were replaced by the lead's strict resubmissions. Round 1 (smoke 261121-214353-49cebb failed in build; rows 261121-214754-909eb7, -214758-617a27, -214802-132bf5, -214806-067a3a, -214810-64032c skipped) is superseded |
| A3 | docs/design/authority-pages.md, "Recommendation" (Limit 1, Q4, D6, D33/D34; quests added) - provisional |
| A4 | [manual] open: the owner's decision, requested at the doc's end |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

6. Commit rows: 4 rotations (a Latin square) instead of 3 interleaved rounds, one strict job each, so every
   job fits 10 minutes and each arm sits once in each position; arms are compared by paired deltas within a
   job. Commit rows run at 07ac6b63 (main 1941ec85 merged: authority src changed, relay builder did not).

5. Before/after for A2 is arm none against the builder arms, interleaved on one node (3 rounds each), and
   seal p50 with min-max over 30 seals for the noise floor; (a) is the before for every option.

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
