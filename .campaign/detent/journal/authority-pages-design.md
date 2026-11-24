# authority-pages-design: journal

State: waiting on jobs 261124-065725-fc6f20
Quest: .campaign/detent/quests/authority-pages-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 after the design review (CHANGES at 65576604, F1-F11) is done in the doc: every ⟨…⟩ marker filled
from the fix-round rows (18937291), main merged (a6bc98d7, tests only: no src or bench change). At head: suite 261124-065709-635779 passed; exclusive 100K seal rerun 261124-065725-fc6f20 running. The lead
cancelled the other seven reruns at a6bc98d7 (acad60, 43d298, e44a71, 8185bb, ca279e, 3d2260, 9ec83f): no new
strict or exclusive jobs until campaign-v3's drain-yields lands. Reruns of the measurement jobs at the merged head
are deferred until the owner decides A4; the merge touched tests only, so the original jobs measure the same src
and bench, and the evidence cites them.

Fix-round rows (at 7a4d6f83; the merge since touched tests only), summarized:
- Paced commit, `check.py pairs` (arm p50 minus adjacent none, ms; mean [95% CI]): 5M exclusive PE
  261123-193517-519b4d: thread -2.0 [-2.8,-1.1], pool -0.2 [-1.2,0.7], write +37.1 [34.8,39.4], aa -0.2
  [-1.1,0.7]; none p50 133.4 ms. Strict P1 f0eafa / P2 65f196: A/A intervals 63 and 119 ms wide, write medians
  +39.9 / +40.2, others inside. 100K strict P3 95a0ae: A/A excludes zero (-157,-5): resolves nothing. Builder at
  0.91 seals/s median per block (0.82-0.98); thread 605.8 ms/seal exclusive (= seal row's 604.8).
- L1 a9b30c steady 2,000/s (strict): T1/i1000 p99 3,315 ms 0.93 core; T10/i1000 1,152 ms 1.29; T4/i250 328 ms
  1.34; T10/i250 396 ms 2.11. LE 962367 (exclusive) T10/i250 295 ms 1.47.
- L2 d6ce25 burst 10,000/s for 10 s: T1 3,200 ms; T4/i250 564 ms 1.75 cores; T10/i250 550 ms 2.55. LE 407 ms 2.03.
- L3 e60802 / L4 1675da failover window (first root past the cold build): T1 15.1 / 11.6 s; T4 6.2 / 3.3 s;
  T10/i250 4.3 / 3.2 / 2.3 s; T10/i1000 4.0 s. LE 2.7 / 2.1 s (T10, exclusive). Peak RSS 0.55-0.69 / 1.0-1.2 /
  1.9-2.0 GB for 1 / 4 / 10 threads.

Blocking findings, each with what confirms its fix (all in docs/design/authority-pages.md; the suite at head,
261124-065709-635779, is the job for the text-only ones):
- F1 (commit rows not at rc3 load; noise above the floor): paced rows at rc3's seal rate with an A/A arm:
  261123-193517-519b4d (exclusive; A/A ±1 ms; pool -0.2 inside, write +37.1 resolved), 261123-193507-f0eafa,
  261123-193510-65f196, 261123-193513-95a0ae; old rows' rate stated and not used for deltas; 24d1dd cited.
  Confirm F1=261123-193517-519b4d.
- F2 (root lag after a leader change understated): window measured end to end, 261123-193525-e60802,
  261123-193528-1675da, 261123-193531-962367 (WINDOW table); agent rule and Limit 1's delay stated (rule 2,
  DELAY). Confirm F2=261123-193531-962367.
- F3 (root not specified per serving path): "Where the root comes from" table (leader, follower/D145, relay per
  tier, agent), root entries in the log, split answers, threat model. Text; confirm F3=261124-065709-635779.
- F4 (pass-through unpriced; Q4 contradiction): pass-through specified, priced and deferred; authority page
  serving priced (~500 MB at 5M) and not recommended; (b) restated as an added cost (1.34-1.75 cores,
  261123-193520-a9b30c, 261123-193523-d6ce25). Confirm F4=261123-193520-a9b30c.
- F5 (decision rows): "What it changes" table names Limit 1, D1, D4, D6, D7, Q4, D34, D133 (with the landed
  code), D145. Text; confirm F5=261124-065709-635779.
Non-blocking: F6 LAG table and coalescing (63b0f2); F7 range quoted; F8 exclusive seal reruns at head deferred
until A4 (fc6f20 only, running); F9 stated; F10 decision request option 5; F11 Limits 4.

## Next

1. When 261124-065725-fc6f20 passes: compare it with the doc's 78.8 ms at 100K (cite it if it agrees), set State
   "ready for review". Queue no strict or exclusive job. The lead accepts the design review with the confirms
   under Doing and launches the correctness reviewer.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/design/authority-pages.md prices (a)-(d) at 100K and 5M, every number citing its job. Seals: exclusive 261122-210428-976f07, 261122-210429-1ed7a8; strict 261123-143709-e27543, -143709-c59049, -143710-993a49, idle -143708-866493. First commit rows (inline ruling): 261123-143701-b2823a, -143704-14eaf8 and 261123-102558-6f96c6 to -102613-0c4d8c. Paced commit: 261123-193517-519b4d (exclusive), -193507-f0eafa, -193510-65f196, -193513-95a0ae. Root lag and window: 261123-193520-a9b30c, -193523-d6ce25, -193525-e60802, -193528-1675da, -193531-962367 (exclusive). Reviewer probes 261123-145400-63b0f2, -145405-24d1dd. All at their named commits; reruns at the merged head (tests-only merge) deferred until A4 |
| A2 | 261123-193433-2e4687 (smoke at 7a4d6f83; the merge since touches tests only); fails without: 261123-102546-6e81cf (with 261123-102550-885bdd). Every row is strict or exclusive at a named commit; before = arm none / option (a), interleaved; noise floor = the aa (A/A) arm's 95% CI (paced), min-max of 30 seals (seals). Suite at head 261124-065709-635779 (passed) |
| A3 | docs/design/authority-pages.md, "Recommendation" and "What it changes": Limit 1 closed with a bounded detection delay (open against a lying relay, Q1); Q4 (b) stands for pages, the authority serves records and roots; D6 unchanged plus split answers; D1, D4, D7, D34 (reason: off the service loop) and D133 amended; quests added: authority-root-entries, authority-builder-pool, relay-authority-root, agent-root-check; none dropped |
| A4 | [manual] open: the owner's decision, requested at the doc's end ("Decision request"). Recommendation, option 2: (b) on four builder threads with 250 ms passes, agents publish and verify at the next root; at 5M 1.3-1.8 cores and a 135-243 MB head on the leader, commit p50 unchanged inside the A/A band, detection delay p99 0.33 s (0.56 s burst, 3.3-6.2 s after a leader change); (d) the fallback at 9.4% of a core per voter |

## Result

docs/design/authority-pages.md, bench/authority_pages/. Recommendation: (b) the leader builds for the root only,
on four builder threads with one pass per namespace every 250 ms, pages in memory, each root proposed as a log
entry; relays forward roots and check their own; agents publish and verify at the next root. (d) the fallback.
- Per seal (exclusive): build 78.8 ms CPU at 100K, 604.8 ms at 5M; (d) 97.3 / 96.7 ms, flat in store size.
- At 5M and rc3's 2,000/s: 1.34 cores steady, 1.75 burst (strict); root lag p99 0.33 s, 0.56 s burst.
- Commit p50 with a builder beside the journal: pool -0.2 ms inside the A/A band of ±1 ms; persisted pages
  +37.1 ms (exclusive 519b4d). Inline stalls the loop 971-2,650 ms: ruled out.
- Leader change: first root past the cold build in 3.3-6.2 s on four threads (strict), 2.1-2.7 s on ten
  (exclusive), at 1.0-1.2 / 1.9-2.0 GB peak RSS.
- Open: A4 and Q1 (signed roots), both the owner's.

## Rulings I made

11. Pool size four, not one per namespace (supersedes 9's "up to one per namespace"): in the same strict jobs ten
    threads held the same root lag as four (p99 0.40 vs 0.33 s steady, 0.55 vs 0.56 s burst) at 1.5-1.6 times
    the CPU and twice the peak RSS, and shortened the window by 0.1-1.9 s. The paced commit rows ran ten threads
    per seal (0.72-0.76 core); four at 250 ms were not run beside the journal (Limits 7).

12. Persisted pages are now resolved: +37.1 ms [34.8, 39.4] exclusive against an A/A of [-1.1, 0.7], so the doc
    rules them out on that number plus simplicity; the review's "not resolved" wording was about the old rows.

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
