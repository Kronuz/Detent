# authority-pages-design: journal

State: waiting on jobs 261123-193507-f0eafa 261123-193510-65f196 261123-193513-95a0ae 261123-193517-519b4d 261123-193520-a9b30c 261123-193523-d6ce25 261123-193525-e60802 261123-193528-1675da 261123-193531-962367
Quest: .campaign/detent/quests/authority-pages-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 after the design review (CHANGES at 65576604, F1-F11). main merged (68e099bd). Harness 7a4d6f83 adds
`paced` (commit path beside a builder posted by the clock at a fixed seal rate; rounds of 8 blocks, none
alternating with thread/pool/write/aa, rotated; `check.py pairs` = block p50 minus mean of the adjacent none
blocks, aa = A/A band) and `lag` (open-loop arrivals at RATE/s with a burst, uniform over the corpus, into a
coalescing pool: one pass per namespace per INTERVAL; warm, or cold = a new leader's cold build while changes
arrive; per-change root lag, window = cold done + first pass). Smoke 261123-193433-2e4687 passed.

Rows (all at 7a4d6f83, --for A2, after the smoke):
- paced 5M (10 x 500,000, 0.98 seal/s, 4 rounds x 40 groups): strict P1 261123-193507-f0eafa, P2 261123-193510-65f196;
  exclusive PE 261123-193517-519b4d. 100K strict P3 261123-193513-95a0ae.
- lag 5M strict: L1 steady 261123-193520-a9b30c (T1/i1000, T10/i250, T10/i1000, T4/i250), L2 burst
  261123-193523-d6ce25 (T1/i1000, T10/i250, T4/i250), L3 failover 261123-193525-e60802 (T1/i1000, T4/i250,
  T10/i250, T10/i1000), L4 failover 261123-193528-1675da (second sample); exclusive LE 261123-193531-962367
  (failover T10/i250 x2, steady and burst T10/i250).

The doc is rewritten for F2-F11 (text); the ⟨…⟩ markers are the numbers these rows fill: LAG-TABLE,
PACED-TABLE, WINDOW-TABLE, DELAY, POOL, REC-NUMBERS, C-COST, WRITE-RULING, FACTS, OPT-B, LAG-ABSTRACT,
COST-ABSTRACT.

Findings, where each is fixed in docs/design/authority-pages.md:
- F1: "Commit latency and elections": old rows' rate stated (139-474 tx/s, 0.09-0.23 seal/s, builder 5-15%
  busy) and not used for deltas; header no longer claims in-job comparison is unbiased; paced rows at rc3's
  seal rate with paired interleaved blocks and an A/A arm (PACED-TABLE); 24d1dd cited for saturation.
- F2: "After a leader change": eleven strict cold samples 11.1-20.1 s CPU, the exclusive 9.0 s, reviewer's pool
  curve and RSS; WINDOW-TABLE end to end; agent rules wait vs publish-and-verify, recommendation 2.
- F3: "Where the root comes from": per-path table (leader, follower/D145, relay every tier, agent), roots as
  log entries, split answers at rooted positions, threat model (unsigned; Q1 for signing).
- F4: pass-through specified (records for every delta, full copy fetched once, roots forwarded) and priced,
  deferred; authority page serving priced (~500 MB at 5M) and not recommended; (b) restated as added cost;
  "What it buys" table and Q4 row agree (authority serves records and roots).
- F5: "What it changes" table: Limit 1, D1, D4, D6, D7, Q4, D34, D133 (with namespace_relay.cc:413/424-426,
  coordinator.cc:685-692), D145: now and becomes.
- F6: coalescing numbers (63b0f2) replace "2.95 cores / pool divides"; pool bound and root-lag metric in the
  recommendation and quests; LAG-TABLE.
- F7: inline quoted as 971-2,650 ms over four jobs, p99 1,020-2,303 vs 234-490.
- F8: one exclusive job each, strict range 1.3-1.8x per seal (783-1,072 ms), 1.2-2.2x incl. cold.
- F9: build CPU includes the records encode, stated (conservative).
- F10: SHA-NI/XOF line in alternative (d) and the decision request's option 5.
- F11: Limits 4.

## Next

1. When the rows pass: `check.py pairs` over P1/P2/PE logs (and P3), summarize L1-L4/LE, fill every ⟨…⟩ marker
   with a job id beside each number, commit, State "ready for review".

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

10. F1, rc3's rate: this host's durable single voter cannot commit 2,000 tx/s (one F_FULLFSYNC per
    64-transaction group). Pipelining 8 batches per group was tried locally and failed: the node admits one
    proposal per service call and the harness drives IO synchronously, so a 512-transaction group took
    ~1,050 ms (8 fsyncs). The paced rows therefore hold the builder at rc3's 0.98 seals/s by the clock,
    independent of the voter's rate; the doc states this as Limits 3.

9. Interval 250 ms and a pool of up to one thread per namespace are the recommended shape; immediate passes
   (no interval) cost 1.08 cores on a 10K-key smoke with 314 relocations, so passes are rate-limited.

8. F4: pass-through is deferred, not part of (b); (b) is restated as an added cost; the authority serves no
   pages (priced). F3: roots as log entries and split answers at rooted positions are the design's answer to
   followers, relays and unrooted positions; signing is the owner's Q1.

7. Lead's ruling (2026-11-23): the exclusive seal rows (261122-210428-976f07, 261122-210429-1ed7a8) are not
   rerun; the doc cites their CPU at bf55a099, and the strict reruns at the merged head are A2's evidence that
   the merge moves nothing (same bytes, same ordering, 29-64% more CPU beside light holds). The doc's seal
   section says both, with each job id.

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
