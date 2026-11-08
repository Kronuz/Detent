# dense-pages review: correctness/performance

Commit: Detent:c1323dba401ff10677d5809292a4785120a6ffbb
Verdict: CHANGES
Date: 2026-11-06

The packing, commitment and ownership mechanisms are coherent, but sparse overlay inspection has an unbounded recursive walk over mutable verified bytes. Required reader measurements and final qualification also remain open. This verdict does not approve landing.

## Evidence I relied on

I read the quest, design, component journals, actual Campaign job records and the following raw logs. These results establish their recorded scope; they do not substitute for final-head qualification.

1. `261105-150743-b66bbc` and `261105-150744-5b7a4c`: native output equals the independently regenerated 100K/5M model, including external placements after segment-id substitution, padding and root. At 5M: 162,367 pages, 315,432,480 page bytes, 302 packs, 315,759,038 pack bytes. Construction records 162,367 page hashes over 306,989,416 bytes, 302 pack hashes over 315,759,038 bytes and 303 metadata hashes over 22,064 bytes. These light jobs establish structure/accounting, not isolated performance.
2. `261106-111601-989a59` at Detent:8d203df: 1,000 seeded trials covering 1..500 changes pass typed Int/Bool body lookup and independent canonical full-build/page-occurrence/path/extent comparisons. The same executable covers repeated content, off-path sealing, no-op rewrites, empty transitions and relocation with zero page hashes. Its actual one-update observation is on 2K definitions: 19 page hashes, 66,498 page-hash bytes and 1,442 written pack bytes; this is not a 5M sparse-write measurement.
3. `261106-112740-2f388f` at Detent:88dc7fc: separate ASan+UBSan build passes 10/10 targeted codec, packer, read, delta, refetch, graph and collection tests. `261106-112739-5f92a5` at the same commit: separate TSan build passes 4/4 threaded reader/bitmap/collection/refetch tests. The subsequent diff through the reviewed commit changes no production source.
4. `261106-070134-ecbe03` at Detent:829f828: authenticated-obsolete-refusal and durable-absence-barrier mutants fail, and restored production passes the real-process refetch/restart suite. The independently generated frozen-v2 fixture and ownership checks avoid treating a reconstructed v3 fixture as old-format evidence.
5. The integration journal records native job `261106-112249-fe30d2` as 175 passed, one skipped and two stale physical-pack assumptions failed. Scoped replacements `261106-113114-03bdbe` and `261106-113406-cadefc` are reported passing. A complete final-head run remains required.

## What I checked myself

1. Read the production diff against main, concentrating on the packer, full builder, overlay/assembler, cold/warm validation, current public readers, verified-start bitmap, recovery/refusal and compaction integration. Full builds and ordinary deltas use the same height-based packing rule; reuse compares content summaries at the corresponding trie path, and extent accounting distinguishes physical ownership from repeated content.
2. Checked the placement-free digest boundaries, reached-header comparisons on hits, bounded public lookup progression, placement-only relocation and removal of the Overflow/second paged commitment mechanism. Remaining logical-named reader/state accessors return the single state commitment and preserve existing interfaces. Recognized v2 framing survives solely for refusal/cleanup.
3. Read the measurement runner and job specifications. It uses current-main Client/pinned-view APIs, real exported stores, exact-value checks, resident-file warming, A/A and counterbalanced runs. The unlanded S6 C ABI is outside this review's prerequisites.
4. Found that baseline/candidate first-read JSONL filenames collide at `tests/detent_system/dense_reader_measure.py:76`. The lead reports correction at Detent:6b58a85, with unique arm filenames and explicit first-read coverage/threshold checks. That correction is outside the reviewed commit and needs its replacement job evidence.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| COR-1 | Yes | Defect | Sparse inspection recursively follows inline slots without progression, depth, visit or accumulated-key bounds. A self/back edge in an already-verified inline branch can recurse until stack exhaustion; shared forward targets can also expand work outside the step budget. | `src/detent_local/paged_trie_overlay.cc:106` (`keys`), `:118` (`inspect`); `View::child` also returns unchecked inline slots. `checked_page_bytes` permits verified hits without rehashing. Static source finding; no crash probe executed. | Bound every inline traversal and key accumulation; require forward slots and a finite page/key/visit budget. Add an ordinary verified-mapping regression that fails before the fix and refuses safely afterward. The lead owns implementation and verification. |
| PERF-1 | Yes | Gap | A15/T5 reader acceptance is pending. No completed evidence establishes all required namespace/publisher arms, warm comparisons, or the current-API concurrency/allocation/syscall conditions. Actual all-definition page visits and real-corpus sparse-write tradeoffs also need reconciliation. | Performance journal; stopped-publisher first-read and A/A/interleaved jobs were queued, not results. Replacement chain reported by lead: `261106-114231-648f9a`, `261106-114232-e75290`, `261106-114233-146c91`. | Inspect successful raw results/configurations and enforce the accepted current-API gates, including 200/200 resident first reads below 1 ms for 1/1,000 namespaces with publisher stopped/running. Record missing arms explicitly; obtain an owner ruling for any gate change. Repeat accepted measurements on main. |

## Not run

No new probe, build, benchmark or fuzz job was executed by this reviewer. Final complete native suite, final-head required hosted CI, completed CPU-hour fuzz/corpus replay, replacement measurement results and post-landing main measurements are not certified here. Existing sanitizer coverage does not exercise COR-1's malformed verified-overlay recursion. Complete file identity remains conditional on derived ids (T8). No other-platform or extended-soak claim is made.
