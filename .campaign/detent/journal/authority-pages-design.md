# authority-pages-design: journal

State: ready for review
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
(--fails-without) passed: it failed with the same NoChange (key 53748); 261123-102550-885bdd at the fix pending.

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

1. Design review (the lead launches it); fold in findings by id. A4 is the owner's.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1 | docs/design/authority-pages.md prices (a)-(d) at 100K and 5M, every number citing its job: seals 261123-103432-c20472, 261123-103435-b1e0ae, 261123-103438-3b96b1, 261123-102616-53d93e (strict, at head) with the exclusive bf55a099 rows 261122-210428-976f07, 261122-210429-1ed7a8 as the quiet-host CPU; commit rows 261123-102553-3c089f, -102558-6f96c6, -102605-a63417, -102610-fdd616 (100K), 261123-102555-afdabb, -102602-d23ae1, -102608-7ae2b2, -102613-0c4d8c (5M) |
| A2 | 261123-102550-885bdd (passed at 300f296b399a; 1 merge from main since, none touching its files; ignores docs-only docs/design/authority-pages.md); fails without: 261123-102546-6e81cf. Measurements: the A1 jobs, all strict at 07ac6b63-equivalent code (300f296b), passed; noise floor = none arm spread and paired-delta ranges (commit), min-max of 30 seals (seals). Earlier: 261122-210412-8d650b smoke; 261123-073248-5fdf61 (replaced by 261123-103438-3b96b1); 261123-073250-7ccbac, 261123-073252-43c224 failed (bench NoChange bug, fixed 07ac6b63); 261122-210431-3080d5, -210432-93640e, -210434-c73864 cancelled; 261123-102306 to -102331 cancelled unrun (zsh option parsing); round 1 261121-214353-49cebb failed in build, its rows skipped |
| A3 | docs/design/authority-pages.md, "Recommendation" and "What it changes": Limit 1 closed, Q4 (b) stands with roots added, D6 unchanged, D33/D34 unchanged; quests added (builder pool + root publication, relay pass-through, agent root check), none dropped |
| A4 | [manual] open: the owner's decision, requested at the doc's end |

## Result

docs/design/authority-pages.md, bench/authority_pages/. Recommendation: (b) the leader builds on a builder pool,
pages in memory, root in D1's frame-header field; (d) the fallback.
- Per seal (exclusive): build 78.8 ms CPU at 100K, 604.8 ms at 5M (59% of a core at rc3 sustained, 2.95 cores
  at burst, beyond one thread); (d) 97.3 / 96.7 ms, flat in store size. 5M cold build 9.0 s. Strict reruns at
  head read 29-64% more CPU, same bytes, same ordering.
- Commit rows (4 rotations, paired deltas): builder thread -2.0 ms (-5.0..+4.8) group p50 at 5M, inside noise;
  persisted pages +13.1 ms (up to +154.9); inline worst service gap 2,650 ms > 2,000 ms election timeout.
- Open: A4 (owner); builder pool and coalescing not measured (implementation quest's); CI run 45238187347
  failed in linux-tsan Configure on a github.com connect timeout (infrastructure, not this branch's files).

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
