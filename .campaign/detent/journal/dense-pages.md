# dense-pages: journal

State: waiting on jobs 261109-000647-5fdbec 261109-000648-f8d372 261109-000649-79f6d4 261109-000650-f03a2a 261109-000651-ba36ce 261109-000652-8732cd 261109-000653-878219 261109-000654-4fbf15 261109-000655-54c96f 261109-000656-ef1c74 261109-000657-33a399 261109-000657-ee6ef2 261109-000658-dd59fb 261109-000659-9cc222 261109-000700-46eab6 261109-000701-61d8db 261109-000702-788b29 261109-000710-6a7c72
Quest: /Users/kronuz/code/Kronuz/Detent/.campaign/detent/quests/dense-pages.md

## Doing

Three harness defects fixed (key grammar, signed segment order, default format limits);
production unchanged. Mutant verdict made explicit (exit 0 only on a test failure). Every proof
job requeued at Detent:98ebd27.

## Next

1. Read the A10 smoke/5M/revert jobs; record T4 actuals against the model projections (5 pages /
   15,691 B one update; 16,616 pages / 42,821,363 B mixed, 99 off-path / 220,903 B).
2. Read the eleven mutant jobs ("MUTANT KILLED" in each log); a survivor gets a test and a rerun.
3. Once the head is stable: `campaign verify dense-pages`; `campaign job rerun` the brief's list
   except 261105-150743/150744 (replaced by the model checks); COR-1's three confirming jobs.
4. Merge main again once namespace-s1-s2 lands (lead's word), then rerun at that head.

## Blocked on

Nothing yet. A15 is the lead's (numbers recorded below).

## Evidence

Audit 2026-11-08, by job record. The quest's src/include/tests equal Detent:a729b77 (the last
full native and sanitizer runs); merging main brought docs only, so gate jobs rerun at the head
for identity, not because code changed.

| Criterion | Evidence |
|---|---|
| A1 | Design `docs/design/dense-pages.md` resolves DP-1..DP-6; contracts review `.campaign/detent/reviews/dense-pages-contracts.md` approves the contracts at Detent:7d339fe. A design record: the lead tags this [artifact] or [decision]. |
| A2 | 261105-150551-bfb8c9 and 261105-150715-4c5b2d pass (codec, packer, segment, snapshot, full builder); full native suite 261106-131622-172969 passes 178 tests at Detent:a729b77. Rerun at head. |
| A3 | Synthetic+100K model check 261108-215336-b92177 passed at Detent:d6d53a9 (synthetic 913 pages / 1,266,995 B, root 30a019eb...; 100K 3,245 pages / 6,209,298 B / 6 packs / 6,217,662 B, root 7c0f7d1b...; exact pages and placements). Kills at d6d53a9 (old harness, exit 1 = killed; each log checked for a test failure): largest-first 261108-215339-d4d8a3 (suites pass; killed by the 100K model compare, test_paged_packer.cc:242), seal-64 261108-215340-a0ba31 (test_local_paged_packer), boundary 261108-215340-ee9bb3 (test_local_paged_packer:284), height-outside-digest 261108-215341-fcf2e6 (10 tests incl. test_local_paged_layout:62). Head (explicit verdict, pass = killed): 261109-000653-878219, 261109-000654-4fbf15, 261109-000655-54c96f, 261109-000656-ef1c74; model 261109-000648-f8d372. |
| A4 | Codec libFuzzer 261106-112828-869301 (927.89 CPU s) and reader/page/graph/warm fuzz 261106-113725-c6d946 (3 x 940 CPU s): 3,747.9 CPU s, zero findings; warm smoke 261106-113537-ede7c2. Corpus replays in the native suite; hosted CI not run: open. |
| A5 | 100K/synthetic 261108-215336-b92177 passed at d6d53a9. 5M 261108-215336-a9a989 aborted: the packer test used default limits (500,000 records), "invalid canonical trie input"; fixed at Detent:98ebd27^ (corpus_limits). Head: 100K 261109-000648-f8d372, 5M 261109-000649-79f6d4. |
| A6 | Forged summary refusal in 261106-111645-b25734. New tests Detent:ddf89ae (forged reference keys/height), Detent:b62b40c (slot off preorder). Kills at d6d53a9 (each a test failure): height 261108-215342-9f84ee and keys 261108-215343-7b0d6f and order 261108-215344-4f774c (test_local_paged_page_validator, the new cases), canonicity 261108-215343-c81dc3 (test_local_paged_graph_validator). Head: 261109-000657-33a399, 261109-000657-ee6ef2, 261109-000658-dd59fb, 261109-000659-9cc222. |
| A7 | 261105-150715-4c5b2d; 5M hash counts page 162,367/306,989,416 B, pack 302/315,759,038 B, metadata 303/22,064 B (261105-150744-5b7a4c). Rerun at head. |
| A8 | Reader fixtures 261106-111645-b25734 and the native suite pass. "Landed S6 interfaces are preserved" cannot hold while S6 is parked: open, the lead's S6/Dense order. |
| A9 | Overlay bounds (COR-1): 261106-114739-ba4d50 Debug, 261106-114740-c11743 ASan/UBSan, 261106-114741-2f126a kills the unguarded parent. S3b's bounds are not on main: open, the S3b/Dense order. |
| A10 | Typed 1,000-round oracle 261106-111601-989a59 (rerun at head pending). Diagnosis 261108-213452-75a571. Smoke 261108-215334-7f2aa1 passed at d6d53a9: 100K one update 5 pages / 14,276 B / 14,287 pack B / 0 off-path; mixed churn (9,800+100+100) 3,090 pages / 5,946,377 B / 6 packs / 5,954,604 B, 2 off-path / 3,203 B; roots equal the model. 5M 261108-215335-b1a3ec aborted, default limits ("invalid dense packer input"), the revert evidence for that fix. Reverts at d6d53a9 killed at the test: key grammar 261108-215337-6fbb5e (line 395), signed order 261108-215338-050e7b (line 25). Head: smoke 261109-000646-d2548f passed, 5M 261109-000647-5fdbec, reverts 261109-000651-ba36ce, 261109-000652-8732cd. |
| A11 | Compaction/relocation fixtures 261106-062527-75b336 (rerun pending). Kills at d6d53a9: off-path skip 261108-215347-fbd883 (paged_trie_overlay, paged_overlay_assembler, dense_delta_assembly:241), relocation rehash 261108-215346-e0fd25 (dense_delta_assembly:221, zero page hashes), no-op rewrite 261108-215345-93d4f7 (paged_delta_leaves, paged_overlay_assembler, dense_delta_assembly:256). Head: 261109-000702-788b29, 261109-000701-61d8db, 261109-000700-46eab6. |
| A12 | Frozen v2 fixture 261106-063837-22f097, refetch/ownership 261106-065403-f3c14e, refusal and barrier mutants 261106-070134-ecbe03. No live references: grep job 261108-215423-6228c3 passed at d6d53a9, head rerun 261109-000710-6a7c72 ( also counts the same patterns on main to show they matched there). Docs [artifact]: `docs/mechanisms/dense-pages.md`, `docs/HOW_IT_WORKS.md` (Dense pages, Format refusal), `docs/mechanisms/verified-page-bitmap.md`, `docs/mechanisms/recovery-digest-once.md`. |
| A13 | ASan/UBSan 261106-112740-2f388f (10/10) and 261106-131702-5c4936; TSan 261106-112739-5f92a5 (4/4); trusted restart byte flips in 261106-131622-172969. CI at the final merged head: not run, open. |
| A14 | Security: CHANGES at Detent:b7b37c6, SEC-1 and SEC-2 gaps, confirming jobs 261106-113537-ede7c2, 261106-112828-869301, 261106-113725-c6d946; the earlier acceptance is void. Correctness: CHANGES at Detent:c1323db, COR-1 fixed and confirmed above, PERF-1 is A15. Architect ruling: none. Open. |
| A15 | First-read acceptance 261106-173358-3b82ed failed the gate, publisher stopped, 200 samples each: below 1 ms baseline 90, Dense 5M 115, 1 namespace 137, 1,000 namespaces 79 (medians 1,031 / 974 / 855 / 1,039 us; zero major faults). A/A and interleaved 261106-132451-040107 cancelled. Open. |

Diagnostic, not evidence: attribution 261106-173446-10c88a, 30 constructions per shape in one
process: first live read 542 us (1 namespace) and 600 us (1,000), of which attach 342 / 329 us,
lookup 201 / 268 us, segment mapping 121 / 168 us.

## Rulings I made

1. 2026-11-08: merged quest/dense-pages--real-deltas before its 5M run passed, so the fix and its
   proof live on the quest branch (no edits in the task worktree). The quest lands only whole, so an
   unproved harness cannot land alone.
2. 2026-11-08: A10's smoke corpus is the extracted 100K store, not the model's synthetic corpus,
   which is outside the key grammar by design (its 200-way branch). The synthetic corpus stays in
   the packer's model comparison, which handles untyped keys.
3. 2026-11-08: mutants follow the design's S1/S2/S4 lists (docs/design/dense-pages.md:311, 329,
   353) as committed patches under tests/detent_local/mutants; A6's "order" is the page
   validator's preorder slot check (T1(4) "a slot off preorder").
4. 2026-11-08: `StateCursor::logical_commitment` and `PreparedReader::logical` keep their names
   on purpose: they return the one state commitment, predate Dense on main, and their callers sit
   in src/detent_delivery (outside this quest's files). A12's removed fields are the format's.

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## Full-builder resumption (2026-11-05)

Reconciled Detent:7ea40e5 and a clean worktree. Peer checkpoints are composed.
The complete 5M corpus and model packs remain available in the parked scratch
worktrees; no extraction is needed. The corrected model expects 162,367 pages,
315,432,480 page bytes, 302 packs and 315,759,038 pack bytes, root
`67c70ad305faa8db45b77749073a144c8e12759ca52bce290f5d7633a3ab7f07`.

Native full-builder proof now checks all three exact hash categories, each
external placement after model-to-native segment-id substitution, canonical
padding, the final root occurrence and model pack count. Its default fixture
includes truly empty admitted input. Reader fixture linkage now includes the
builder, and disk-fixture ownership declares the current format, as requested
by the reader task and approved by the lead.

Baseline job: `261105-150551-bfb8c9` at Detent:7ea40e5 passed all five
Debug codec, packer, segment, snapshot and full-builder tests (19.77 seconds). The first
submission `261105-150515-44cd1a` was cancelled: campaign resolved literal HEAD
against the home checkout, so it named main and is invalid candidate evidence.
Until that tool issue is fixed, submit the explicit `git rev-parse HEAD` result.

Queued at Detent:f444c98:

1. `261105-150715-4c5b2d`: targeted Debug build and empty/full-builder,
   snapshot resource/recovery and sealed-segment tests; passed, exit 0. Exports
   the exact binary for the dependent corpus jobs.
2. `261105-150743-b66bbc`: fresh 100K Python model encoding, corpus/model/binary
   hashes and native full-generation comparison (dependent on the targeted job).
3. `261105-150744-5b7a4c`: the same comparison on 5,000,004 corpus entries.

These are light correctness jobs. Their printed CPU/wall observations are not
isolated performance acceptance measurements. When they finish, reconcile the
raw output, address failures, and add mutation evidence for strengthened oracle
and accounting assertions before claiming A2/A3/A5/A7 complete.

## Reconciled after quota reset

Both native/model jobs completed successfully: 261105-150743-b66bbc (100K) and 261105-150744-5b7a4c (5M). Actual 5M: 162,367 pages, 315,432,480 page bytes, 302 packs, 315,759,038 pack bytes, root 67c70ad305faa8db45b77749073a144c8e12759ca52bce290f5d7633a3ab7f07. Page hashing 162,367 calls / 306,989,416 bytes; pack hashing 302 / 315,759,038; metadata 303 / 22,064. The executable compares exact external placements, padding and root against the independently regenerated model. Timings are light-job observations and establish no speed improvement.

Next: finish targeted mutant/resource proof and integrate final readers/deltas/ownership. Whole-feature acceptance and reviews remain open. Campaign quota now comes from Codex logs; a single Sol/medium delta trial runs before more developer concurrency.

## Whole-feature compile reconciliation

The first native whole-tree build exposed old test-only physical_commitment and Root.logical uses. Segment fixtures now use pack_commitment; root and lease fixtures use metadata_commitment; state checks use state_commitment or existing tree equality. The unsupported ownership test now forges version4 instead of the active3. No old public alias is reintroduced.

The old trie-cursor fixture still built removed one-node pages. It now compares every emitted typed event, prefix, child label, ordinal and key count against independent recursive key grouping, retaining byte/row budget, cancellation and admission checks. Full-build/graph fixtures cover the separate dense packer integration.

Whole-tree coordinator compile failure is reproduced unchanged on main by261106-063838-006684; its fixture now explicitly includes the four Asio APIs it already uses. Dense fixtures now exercise retained-pack pressure with a genuinely self-sealed unchanged Boolean definition (5,000-byte selector), preserving exact pre/post values and retirement assertions. Tiny-page and cursor budget assumptions are corrected without relaxing rejection/resource claims.

The retained-Boolean fixture uses32 valid128-byte selectors (within the200-byte wire token limit), preserving the original beta selector and all value checks. The bitmap mark-race fixture now selects a real external dense page. Mapped-fault tests mutate the actual private mapping under temporary mprotect, since file writes need not alter an existing MAP_PRIVATE view; no production mapping permission changes.

## Quota-aware checkpoint

S5 final restart-lifetime fix is composed from Detent:dcd7d0c; frozen v2, exact refusal/refetch and both semantic mutant jobs pass. Commit-wakeup returns the integration branch and journal state; no finished-agent result is left unattended.

The latest9-test repair run passes cursor, reuse, publication-artifact and dependency-walk tests. Remaining fixture work: retained Boolean selectors must be strictly ordered (corrected here); lazy bitmap collision tests still need genuine multiple segments instead of several references to one dense pack; collector pressure retry variant13 needs a geometry-aware resource fixture; disk-store/collection publication are rerun after ordered selectors. Dense mapped-framing corruption passes after mutating the actual private mapping.

No format aliases were reintroduced. PreparedGeneration/StateCursor/Reader logical-named state accessors still carry the single state commitment; retain the public reader interface while auditing terminology. Remaining full-feature obligations include1,000 delta rounds, sanitizers, bounded fuzz, measured reader/write acceptance, and final whole-feature reviews. Scope remains Dense pages only.

The bounded delta oracle now accepts a rounds argument, retains50 rounds for the fast suite, and spans1..500 input changes cyclically. The1,000-round gate runs the same independent full-build/value/path/extent oracle twice across every mutation-count size. No timing win is claimed.

The1,000-round raw-payload oracle passes at Detent:4986f72. The stronger follow-up uses valid encoded Int/Bool definitions with realistic override bodies and independently looks up every target key/body through PagedReadState, in addition to canonical page/path/extent comparisons. Artificial sealing-boundary cases remain explicit structural fixtures.

## Integrated validation checkpoint, 2026-11-06

1. Typed 1,000-round delta oracle passed: 261106-111601-989a59.
2. Canonical bitmap and isolated collector fixtures are merged at Detent:88dc7fc. Scoped proof 261106-111645-b25734 passed, including repeated collector runs.
3. Full native job 261106-112249-fe30d2 passed175 tests, skipped one platform case, and failed two obsolete pack-count fixture assumptions. T5 passes in261106-113114-03bdbe; the genuinely three-pack 99,990-integer scrub passes in261106-113406-cadefc. Final complete-suite rerun remains required.
4. ASan+UBSan261106-112740-2f388f passed10/10; separate TSan261106-112739-5f92a5 passed4/4. Production code is unchanged by the subsequent fixture/fuzz additions.
5. Whole-feature security review Detent:d136be3 found no production defect. SEC-1 requires randomized warm validation; sanitizer smoke261106-113537-ede7c2 passed455 samples in20.0054 CPU seconds. SEC-2 requires completed CPU-hour evidence. Jobs261106-112828-869301 and261106-113725-c6d946 remain in flight. Submission261106-113716-9a11c4 had an incorrect shell command and is invalid evidence despite exit0.
6. Performance task exported authentic100K/5M candidate stores and checked actual public-client values in261106-112936-d91c6d. Contended smoke timings are not acceptance evidence. Isolated reader measurements and the one/1,000-namespace cases remain pending. Current-main Client/ReadView is the API baseline; the unlanded S6 C ABI is outside this Dense-only campaign.

Next: finish measurements and fuzz, replay generated corpus, rerun final native checks, obtain whole-feature correctness/performance and architect landing reviews, confirm security gaps with job evidence, then land once all criteria pass.

## Owner park, 2026-11-06

The owner requests a takeover checkpoint to conserve credits. No new work is started. The remaining benchmark, first-read, real-delta smoke/full and profile-build jobs are cancelled; partial output is retained but does not establish acceptance. Campaign status reports zero queued/running jobs and quiet holds are drained. Resume from the home checkout handoff docs/handoffs/dense-pages-2026-11-06.md, preserving the Claude lead's broader campaign scope.

## Owner resumption, 2026-11-06

The owner resumes this bounded work with remaining credits. The Claude takeover keeps its full scope; this session continues only Dense proof. New jobs replace the cancelled smoke/full delta pair, first-read check and profiling diagnostic. No new adventurer or production change is launched.

## Results of the resumed jobs, recorded 2026-11-08

The four jobs submitted at the owner resumption ended without being recorded. Smoke 261106-173308-d6523a failed (exit 134, ReadSourceFailure: local page walk failed); 5M 261106-173308-538815 skipped. First-read acceptance 261106-173358-3b82ed failed its gate (A15 row). Profile build 261106-173309-40965c and attribution 261106-173446-10c88a passed (diagnostic only).

## Session 2026-11-08 (implementation contributor)

A10 diagnosis. The smoke ran the model's synthetic corpus, whose "wide" namespace holds keys
`Kwide\0a<c>\0` for c in 33..232: '!' and bytes above 127 are outside the store key grammar
(`is_name_char`). The packer seals them; `PagedReadState::lookup` decodes each reached leaf's key,
`decode_key` throws `invalid_argument`, and the catch-all maps it to `ReadSourceFailure: local page
walk failed`. The read path is correct; the harness fed it a store no client can write.
Prediction "record 20000 is the first bad key" (20,000 `Kload*` keys sort first) confirmed by job
261108-213452-75a571 at Detent:be49a1d.

Fix (Detent:be49a1d): the corpus parser checks every key with `decode_key` and names the record;
a fast case (`check_key_grammar`) pins the parser refusal and the reader's `ReadSourceFailure` on a
built store holding `Kwide\0a!\0`; the smoke runs the pinned extracted 100K corpus. Revert check
261108-213503-7538e7 (must fail): parser without the refusal.

Jobs at Detent:be49a1d: smoke 261108-213453-4048a8 (100K), 5M 261108-213503-22cf8f (after smoke).

Mutants (Detent:ce98e70 scripts, run at Detent:45a46aa, slot dense-mutants; each must fail with
"MUTANT KILLED"; exit 3 means no verdict): a3-largest-first 261108-214257-9e49ac, a3-seal-64
261108-214258-9d23ef, a3-boundary 261108-214258-dcbe50, a3-height-digest 261108-214259-27af36,
a6-height 261108-214300-2f03c3, a6-keys 261108-214301-6102a5, a6-canonicity 261108-214302-8544d8,
a6-order 261108-214302-c5dc67, a11-noop-rewrite 261108-214303-a9d58a, a11-relocation-rehash
261108-214304-1e5d5d, a11-offpath-skip 261108-214305-3204ec. The A3 ones also run the 100K model
check when the suites pass.

Model checks at Detent:45a46aa: 100K+synthetic 261108-214256-fab375, 5M 261108-214256-99a747
(`docs/perf/dense-model-check.sh`; replaces 261105-150743/150744, which ran a binary copied out of
another job and cannot be rerun at a new head).

Main merged at Detent:45a46aa (docs and campaign files only; one status-line conflict in
`docs/status/dense-pages.md`, taken as main's HISTORY line).

Superseded: the 45a46aa model and mutant jobs were cancelled before running (new tests below).

Smoke 261108-213453-4048a8 at Detent:be49a1d: the 100K one-update arm PASSES exactly against the
model (5 pages, 14,276 B written, 14,287 pack B, 0 off-path, root 794d3bbf...); the mixed arm
(10,004 changes) threw `invalid_argument: invalid paged segment reference`. Cause: the oracle
sorted segment inputs with `std::array<char>::operator<` (signed chars here) while `find_segment`
searches with memcmp; the six output packs' derived ids (and base ordinals from 128, e.g. 5M's 302
packs) break the order, and `inputs[find_segment(...)]` indexed past the end. Harness defect, not
production (every production sort and search uses memcmp). Fix Detent:8056246: memcmp order, a
checked `segment_index`, a 600-page one-page-per-pack base case; paged_split_fixture.hh and
test_pack_scrub.cc had the same signed sort and now use memcmp. Revert key-grammar check
261108-213503-7538e7 failed at the parser's `require(refused)` (line 387) as required.

New tests from mutant analysis (before any mutant ran): the page validator replay checks the
header against content, so header byte flips cannot isolate the constructor's reference/header
comparison; Detent:ddf89ae forges reference keys/height over a valid page. The validator walks in
byte order, so a slot off preorder is caught only by `slot == position`; Detent:b62b40c swaps two
inline slots of a checksum-correct page.

### Head d6d53a9 jobs

A10 smoke 261108-215334-7f2aa1, 5M 261108-215335-b1a3ec (after smoke). Model checks 100K
261108-215336-b92177, 5M 261108-215336-a9a989. Revert checks (must fail): a10-key-grammar
261108-215337-6fbb5e, a10-signed-segment-order 261108-215338-050e7b. Mutants (must fail):
a3-largest-first 261108-215339-d4d8a3, a3-seal-64 261108-215340-a0ba31, a3-boundary
261108-215340-ee9bb3, a3-height-digest 261108-215341-fcf2e6, a6-height 261108-215342-9f84ee,
a6-keys 261108-215343-7b0d6f, a6-canonicity 261108-215343-c81dc3, a6-order 261108-215344-4f774c,
a11-noop-rewrite 261108-215345-93d4f7, a11-relocation-rehash 261108-215346-e0fd25,
a11-offpath-skip 261108-215347-fbd883.

## Lead message 2026-11-09 (5M aborts, mutant verdict)

1. Both 5M jobs at d6d53a9 aborted before comparing anything: the packer test and the delta oracle
   built their trie cursor, packer, overlay and assembler with default `paged::Limits` (500,000
   records, 256 MiB page bytes), and the 5M corpus has 5,000,004 keys and 315 MB of pages
   (cursor: "invalid canonical trie input"; packer: "invalid dense packer input"). Harness defect;
   the full-build test already sized its limits. `corpus_limits` (dense_fixture.hh) sizes every
   oracle's limits to its corpus, keeping the defaults for smaller fixtures. Revert evidence: the
   two aborted jobs at the parent commit.
2. Mutant verdict: dense_mutant.sh exits 0 only when a test fails (ctest exit 8, or a
   DENSE_MUTANT_RUN failure other than its setup's exit 3); a survivor exits 1; no patch, no
   build, no matching test or a tool error exits 3. dense-model-check.sh exits 3 on setup
   failures. Self-test 261109-000650-f03a2a requires no-build = 3 and a comment-only mutant = 1.
3. Every d6d53a9 mutant/revert log was read: each ended "MUTANT KILLED" after a named test failed
   (lines in the Evidence table), none at a build. They ran under the old convention (exit 1 =
   killed); the head reruns use the explicit one.
4. Main merge held until the lead says S1/S2 landed.
