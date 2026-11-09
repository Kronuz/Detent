> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning.

# Dense pages takeover checkpoint

Date: 2026-11-06. Status: parked at the owner's request for credit conservation. This is the return handoff for one delegated task. The Claude lead retains its own broader campaign scope and master plan, including its other workstreams and repositories. No Dense implementation is landed or pushed to GitHub. All branches and job artifacts below are local to this machine.

## Start here

1. Read `claude-lead-prompt.md`, then the installed Campaign 2.0 skill. Dense pages is already migrated; reuse the existing campaign.
2. In `/Users/kronuz/code/Kronuz/Detent`, run `campaign status`, `campaign check` and `quiet list`. The final park has zero agents and zero queued/running jobs. Remaining jobs are cancelled and their partial outputs are retained.
3. Feature worktree: `/Users/kronuz/code/Kronuz/Detent-wt/dense-pages`, branch `quest/dense-pages`. The home checkout's main contains campaign metadata and reviews, not the feature code.
4. Current feature checkpoint: Detent:c4dc315. Production overlay fix: Detent:506c246, integrated through Detent:a729b77. Later task branches contain unmerged test/profiling work.
5. Read `docs/campaign/quests/dense-pages.md` for A1–A15 and `docs/campaign/journal/dense-pages.md` on the feature branch. The broader historical plan remains in `docs/handoffs/detent-codex-2026-11.md` and the old workstream ledgers.

## Implementation and evidence

The composed feature uses DTPAGE03/DTROOT03, canonical dense packing, one placement-free state commitment, full record copies and whole-pack checksums. Full builds, sparse deltas, compaction, relocation, reads and authenticated v2 refusal/refetch use that format. Public logical-named state accessors carry the single state commitment; there is no second logical hash mechanism.

| Evidence | Job | Result |
| --- | --- | --- |
| Full native suite before final overlay integration | 261106-114101-4b6bf8 | 177 executed tests pass; one platform skip |
| Full native suite with overlay fix and retained fuzz corpus | 261106-131622-172969 | All 178 executed tests pass; one platform skip, 371.40 seconds |
| Integrated ASan+UBSan | 261106-112740-2f388f | 10/10 pass |
| Separate threaded-path TSan | 261106-112739-5f92a5 | 4/4 pass |
| Final overlay/delta/corpus replay ASan+UBSan | 261106-131702-5c4936 | Pass |
| Typed delta oracle, 1,000 rounds of 1..500 changes | 261106-111601-989a59 | Exact looked-up bodies, canonical occurrences, path reuse and extents pass |
| Canonical bitmap/collector fixtures | 261106-111645-b25734 | Scoped tests and five repeated collection runs pass |
| Actual frozen v2 refusal/refetch and semantic mutants | 261106-070134-ecbe03 | Both mutants fail; restored production passes |
| Codec ASan+UBSan libFuzzer | 261106-112828-869301 | Zero findings; 927.89 CPU seconds |
| Reader/page/cold-graph/warm fuzz, three seeds | 261106-113725-c6d946 | Zero findings; 940.026+940.028+940.001 CPU seconds |

Fuzz total: 3,747.945 CPU seconds. The generated codec corpus is retained at Detent:6485a9e and replayed by the ordinary CTest target. Submission 261106-113716-9a11c4 is invalid evidence despite exit 0 because its shell command was incorrect.

The full native failures were fixture assumptions about sparse v2 pack counts. The integer scrub now uses 99,990 records and genuinely spans three packs. Trusted-restart T5 retains 100 byte flips, exact value checks and scrub checks while requiring several dense packs. Their repaired jobs pass. The slow native tests require realistic timeouts; use 600 seconds, not 180.

The canonical bitmap fixture has 77 sealed pages in 27 packs. Its scoped run reports 113 verifications, 36 concurrent duplicates and 56 refused neighboring corruptions. Collector pressure uses a dedicated child account; 64 KiB headroom is the measured failing-first/succeeding-retry case. Do not replace it with guessed aggregate accounting.

## Whole-feature reviews

1. `docs/campaign/reviews/dense-pages-security.md`: no production defect, initially CHANGES for warm randomized coverage and completed CPU-hour evidence. Both gaps are confirmed by the fuzz jobs above. `campaign accept` records security at Detent:a729b77.
2. `docs/campaign/reviews/dense-pages-correctness.md`: COR-1 identifies unbounded inline overlay counting; PERF-1 leaves quantitative gates open. COR-1 is fixed by Detent:506c246 with a 203-frame stack, 202-byte depth, node-visit budget, checked sums and forward inline slots. Debug 261106-114739-ba4d50 and ASan/UBSan261106-114740-c11743 pass; bounded parent-code regression 261106-114741-2f126a fails as required. The fix preserves verified-hit zero hashing. Confirm with these jobs rather than commissioning a whole re-review.
3. Correctness/performance acceptance remains open. The architect has not given the final landing ruling. Required CI has not been started for the assembled branch.

## Actual large-corpus results

| Corpus | Pages | Page bytes | Packs | Pack bytes |
| --- | ---: | ---: | ---: | ---: |
| 100K definitions | 3,245 | 6,209,298 | 6 | 6,217,662 |
| 5M definitions | 162, 367 | 315,432,480 | 302 | 315,759,038 |

Jobs261105-150743-b66bbc and261105-150744-5b7a4c reproduce the independent model's root, placements, padding and all three hash categories. At 5M, page hashing is162, 367 calls/306,989,416 bytes, pack hashing 302/315,759,038, metadata 303/22,064. Admitted source memory is 783,597,840 bytes; output resident 316,793,408; peak 1,100,554,777. These are correctness/light jobs, not speedup measurements.

The all-definition public walk in 261106-113938-0f7177 counts distinct physical pages across definition and watermark lookups: 2 pages for 6 definitions, 3 for 54,149, 4 for 756,570, 5 for 2,580,975, 6 for 1,608,300. Median 5 and p99 6 match the model.

## Reader acceptance and the S6 boundary

Current main does not contain `include/src/tests/detent_reader` or `benchmarks/reader_bench`. The separate S6 branch remains parked. The current public Client/ReadView APIs are the measured baseline in this focused Dense campaign.

`docs/design/reader.md` explicitly says the existing Client misses 1 ms in three of four prior arms; its T2 is a target for the proposed S6 core. Dense A15 carries that target. The full-plan lead must reconcile S6 integration and the Dense consumer changes before final acceptance. Keep the threshold explicit; do not claim every baseline miss is introduced by Dense or make the Dense developer inherit all S6 implementation.

Strict functional smoke 261106-132450-077e0a passes exact values. Its candidate first-read observations have zero major faults but miss the absolute target:

| Candidate shape | Below 1 ms | Median ns | Maximum ns |
| --- | ---: | ---: | ---: |
| 5M, four namespaces | 23/40 | 948,406 | 1,281,675 |
| 5M, one namespace | 32/40 | 835,228.5 | 1,253,518 |
| 5M+999x100, 1,000 namespaces | 16/40 | 1,041,923.5 | 1,342,997 |

A 1,000-namespace sample splits into 617,207 ns attachment and 420,515 ns first lookup, 5 verified pages/11,214 hashed bytes, 5 record opens and 880 table bytes. These are smoke observations. The 200-sample confirmation and A/A/interleaved results below decide acceptance. No production optimization is claimed.

The meter retains baseline/candidate raw files separately, checks required arms and makes `--mode first` fail unless both required candidate shapes achieve 200/200 below1 ms. Fix: Detent:6b58a85. Ruff formatting follows at Detent:6485a9e. Stage261106-131616-72899e has symlinked stores and is unsuitable for secure public opening; replacement261106-132438-20983c uses APFS clones in real directories. Preserve O_NOFOLLOW behavior.

## Unmerged task work and parked jobs

| Branch/worktree suffix | Head | State at park |
| --- | --- | --- |
| `quest/dense-pages--performance` / `dense-pages--performance` | Detent:dd981e9 | Clean, parked; test-only profiling code at Detent:ff1658d |
| `quest/dense-pages--real-deltas` / `dense-pages--real-deltas` | Detent:829ee5b | Clean, parked; real-corpus harness at Detent:5aad259, compile fix Detent:d051e91 |
| `quest/dense-pages--overlay-bounds` | Detent:8967d2b | Complete, merged |
| `quest/dense-pages--reads` | Detent:134c18e | Complete, merged |
| `quest/dense-pages--deltas` | Detent:6c07aa2 | Implementation merged; stale journal state is historical |
| `quest/dense-pages--integration` | Detent:dcd7d0c | Implementation/proof merged; stale journal state is historical |

Worktrees live under `/Users/kronuz/code/Kronuz/Detent-wt/`. Each task's singular `docs/campaign/journal/<task>.md` is authoritative. No adventurer remains running at park. The profile task has no production change. Its `--profile` loop reuses process/runtime state and cannot prove fresh-process acceptance.

| Job | Purpose | Final park state |
| --- | --- | --- |
| 261106-132451-040107 | Exclusive public-reader A/A plus four interleaved 5M comparisons | Cancelled; partial output retained |
| 261106-132451-1f5ce0 | Exclusive200 resident first reads per shape | Cancelled before execution |
| 261106-132614-fcb023 | Real-delta synthetic model/native smoke | Cancelled |
| 261106-132621-774d99 | Dependent actual 5M one-update and mixed-churn oracle | Cancelled before execution |
| 261106-132922-093a04 | Test-only profile probe build | Cancelled |

All five jobs are cancelled at the final owner park. The table preserves their purpose and final state; none supplies completed acceptance evidence. The interrupted warm comparison's raw runs remain available as partial observations. Resubmit from the recorded specs at the appropriate resumed code and rebuild binaries when that code changes.

Job records are under `~/.scratch/campaign/jobs/ID/`: `spec.json`, `state.json`, `a1/log`, `a1/out/`. `campaign job log ID` is only a tail; search the actual log for early compiler errors. Measurement jobs write `a1/out/results/result.json` and retain raw JSONL samples. No new profiling sample job is authorized by this park.

The real-delta task adds an optional corpus mode to `test_dense_delta_assembly.cc`; default fast tests remain. Its job driver is `docs/perf/dense-delta-real-corpus.sh`. It compares every looked-up byte body/deleted miss and canonical occurrence, retained extents, unique physical ownership and exact new page, pack and off-path totals. Model bodies are opaque for that structural comparison; do not claim public typed validity beyond what it checks. Expected T4 projections are 5 pages/15,691 bytes for one update;16,616 pages/42,821, 363 bytes for 9,800 updates+100 inserts+100 deletes, including 99 off-path pages/220,903 bytes. These projections are unproven until its actual jobs pass.

## Machine and artifact details

1. Current installed tool snapshot: dotfiles:e1ea102 on clean main; agent-campaign:28683a0 on clean main. Inspect again because other sessions update it. Earlier Codex tooling fixes are merged. No campaign-tool change is needed to adopt Dense pages.
2. Tooling work includes Codex usage/quota readers and commit/mailbox waits. Claude usage is not assumed implemented; inspect `USAGE_READERS`. `pace claude` still needs a usable local quota record or status-line snapshot. Preserve harness portability and the instruction word budget.
3. Apple Clang works with `-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT=`. Explicit SDK and LLVM23 CMake configurations caused compressor/header failures. Separate sanitizer builds have separate dependency trees.
4. ccache is active. `base_dir=/Users/kronuz` covers both code and job checkouts. Controlled two-directory compilation261106-064157-c186f2 changed 0 hits/2 misses to 1 hit/1 miss. The dotfiles fix is dotfiles:e09fbfa; do not claim a timing win from it.
5. Baseline Detent:785fb34 is detached at `.claude/worktrees/dense-pages-baseline`; its Release tools are under `.scratch/build`. Preserve it. Baseline stores and recipes: `.scratch/capacity-trials/100000-r3` and `5000000-restore-r12`; stores are `agent-0/generations`, recipes `expected-values.bin`.
6. Real 100K corpus: `.claude/worktrees/dense-pages-series/.scratch/dense-s1/corpus-100k.bin`. Real 5M corpus: `.claude/worktrees/agent-a7d1b230a685025e4/.scratch/dense/corpus-5m.bin`. All paths are relative to the home Detent checkout. Hashes and model commands are in the successful job records.
7. Reader fixture preparation261106-113938-0f7177 retains baseline clones, real dense stores, derived 1/1,000-namespace stores and T3 output. Final copied binaries and secure directory clones live in 261106-132438-20983c. Do not mutate completed job artifacts.
8. Detent remote `github` points to Kronuz/Detent. `origin` is a stale local path. CI does not automatically cover every `quest/**` branch; inspect triggers and dispatch the required workflow explicitly if needed. No feature push or final CI result is claimed here.
9. Task file scopes for the completed reads/integration writers are reduced to their status docs so the performance task can own the three reader source groups. This releases ownership; it does not discard their commits. No active writer may overlap them.
10. `campaign check` has no contradiction errors. It warns that the installed tool prefers migrating legacy `docs/campaign` metadata with `campaign migrate` when no quest is in flight. Dense remains an active, parked quest; defer that metadata migration until its preconditions hold.

## Next actions for the full-plan lead

1. Reconcile the parked journals. Resubmit the cancelled proof jobs needed by the resumed plan, using their specs and correct code. Do not treat partial runs or cancelled/superseded pipelines as completed evidence.
2. Merge the real-delta test task only after its smoke and actual corpus comparison pass; investigate discrepancies with exact model inputs. Keep unproven counts explicit.
3. Decide the S6/Dense integration order from the full plan and actual baseline/candidate evidence. Continue bounded attribution of first-read cost if it serves that order. Profile code is available; no speculative production optimization is pending.
4. Finish A1–A15, including quantitative read/write evidence, outstanding mutants where needed and mechanism documentation. Current-main substitutes do not establish S6's missing C ABI, thread, fork, allocation or syscall gates.
5. Accept correctness with the confirmed COR-1 fix and completed quantitative jobs. Obtain the architect's final review once. Rerun checks at the final combined code, run required CI, land the format switch once and repeat accepted measurements on main. Defer extended matrices/soaks as the owner directed.
