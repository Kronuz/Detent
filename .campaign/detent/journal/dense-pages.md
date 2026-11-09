# dense-pages: journal

State: waiting on jobs at the merged head Detent:435a525 (gate 261109-145341-9f0788, T7 positive 261109-145355-7c2f59, the evidence batch under "Merged head 2026-11-09") and the A15 gate rerun f406a9 -> 210773
Quest: /Users/kronuz/code/Kronuz/Detent/.campaign/detent/quests/dense-pages.md

## Doing

Merged main (S1/S2, Detent:ff6218e) and the performance task at Detent:435a525; gate, T7
positive, the S1/S2 startup-before-barrier mutant and every evidence job queued there. 500K
round 3 closed: fix proven (verdicts below).

## Next

1. Read 9f0788 (gate), 7c2f59 (T7 positive, must pass), 0a4f36 (startup-before-barrier, must be
   killed), then the batch; rewrite the Evidence table with the head ids.
2. Fold the head ids into A9/A10/A11 (COR-1 confirm: bd9ad1, 71a2c4, cecb4a) and A4/SEC-2
   (fuzz 649826, 976098, warm smoke 2539d5) for the lead's accepts.
3. A15 gate rerun (lead ruling, msg of 2026-11-09): prepare 261109-151355-f406a9 -> exclusive
   measure 261109-151427-210773; read result.json per arm. The lead decides the gate.
4. d4859c is contaminated, not evidence; its own-slot rerun is 261109-145822-623329.

## Blocked on

Nothing. A15 is the lead's (numbers recorded below).

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
| A9 | COR-1 at Detent:c9db691: Debug 261109-023940-16ed87, ASan/UBSan 261109-023941-feea7a, unguarded-parent kill 261109-023943-824414 (probe now a patch, `mutants/cor1-unguarded-parent.patch`). Earlier, overlay bounds (COR-1): 261106-114739-ba4d50 Debug, 261106-114740-c11743 ASan/UBSan, 261106-114741-2f126a kills the unguarded parent. S3b's bounds are not on main: open, the S3b/Dense order. |
| A10 | At Detent:ac4c32e: smoke 261109-002832-7724ff passed; 5M 261109-002833-ba74d7 aborted (default limits in the overlay, production; fixed Detent:791d9fc). At Detent:c9db691: smoke 261109-023944-e56196, 5M 261109-023945-fd8f8f, reverts (pass = killed) overlay 261109-023934-cdb214, cursor 261109-023935-fe85d6, key grammar/segment order killed at ac4c32e (261109-002839-00fc85, 261109-002840-cdfa5b), 1,000 rounds 261109-023959-b149d6. Earlier: typed 1,000-round oracle 261106-111601-989a59 (rerun at head pending). Diagnosis 261108-213452-75a571. Smoke 261108-215334-7f2aa1 passed at d6d53a9: 100K one update 5 pages / 14,276 B / 14,287 pack B / 0 off-path; mixed churn (9,800+100+100) 3,090 pages / 5,946,377 B / 6 packs / 5,954,604 B, 2 off-path / 3,203 B; roots equal the model. 5M 261108-215335-b1a3ec aborted, default limits ("invalid dense packer input"), the revert evidence for that fix. Reverts at d6d53a9 killed at the test: key grammar 261108-215337-6fbb5e (line 395), signed order 261108-215338-050e7b (line 25). Head: smoke 261109-000646-d2548f passed, 5M 261109-000647-5fdbec, reverts 261109-000651-ba36ce, 261109-000652-8732cd. |
| A11 | Mutants at Detent:c9db691 (pass = killed): noop-rewrite 261109-023936-80095a, relocation-rehash 261109-023937-d09b48 (patch now names the assembler's limits), offpath-skip 261109-023939-b86619; all three killed at ac4c32e (261109-002853-0e22ed, 261109-002854-cecb99, 261109-002855-28f894). Compaction/relocation fixtures 261106-062527-75b336 (rerun pending). Kills at d6d53a9: off-path skip 261108-215347-fbd883 (paged_trie_overlay, paged_overlay_assembler, dense_delta_assembly:241), relocation rehash 261108-215346-e0fd25 (dense_delta_assembly:221, zero page hashes), no-op rewrite 261108-215345-93d4f7 (paged_delta_leaves, paged_overlay_assembler, dense_delta_assembly:256). Head: 261109-000702-788b29, 261109-000701-61d8db, 261109-000700-46eab6. |
| A12 | Frozen v2 fixture 261106-063837-22f097 at Detent:785fb34 (valid only there: the head refuses to encode version-2 ownership, test_ownership.cc:79; its head rerun 261109-002857-957eef fails by design). Head identity: embedded bytes equal its printed hashes, 261109-023948-15181a. Refetch at head 261109-023956-269318, barrier mutants 261109-023957-39d01e, grep 261109-023950-68e477. Earlier: refetch/ownership 261106-065403-f3c14e, refusal and barrier mutants 261106-070134-ecbe03. No live references: grep job 261108-215423-6228c3 passed at d6d53a9, head rerun 261109-000710-6a7c72 ( also counts the same patterns on main to show they matched there). Docs [artifact]: `docs/mechanisms/dense-pages.md`, `docs/HOW_IT_WORKS.md` (Dense pages, Format refusal), `docs/mechanisms/verified-page-bitmap.md`, `docs/mechanisms/recovery-digest-once.md`. |
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
6. 2026-11-09: the codec's `Limits` default is removed rather than patching the three callers: one
   explicit argument everywhere makes the omission a compile error. Callers on validated bytes
   (cursor, reuse navigator) take `paged_format_limits()`, as their neighbouring decodes already
   did; the overlay passes its own. Contextual reuse has no >504,096-key test (compile-enforced
   only); overlay and cursor have one each, with revert patches.
7. 2026-11-09: 261106-063837-22f097 is not rerun at the head: it compiles the v2 generator against
   Detent:785fb34 by design (`dense_legacy_fixture_generator.cc:1`). The head check is that
   `dense_legacy_fixture.hh` embeds exactly its output (`dense_legacy_fixture_check.py`).
8. 2026-11-09: A3/A6 mutant kills stand at Detent:ac4c32e (packer and validators unchanged since);
   the A11 mutants and both COR-1/A10 reverts rerun at the head because the overlay changed.
   Pinned corpora now live at `/Users/kronuz/code/Kronuz/Detent/.scratch/dense/` (same sha256),
   named by `DENSE_CORPUS` in each job's env.
5. 2026-11-09: the two one-CPU-hour fuzz jobs (261106-112828-869301, 261106-113725-c6d946) are
   rerun once, at the post-S1/S2 head, not now: the decoders are unchanged since they ran and a
   second hour at a head that will change again buys nothing. Lead may overrule.
9. 2026-11-09 (lead ruling, msg on the gate failure): the 500K race is fixed in the fixture, not
   in src/detent_delivery. The transitions mode's final close now gets 204 like every other close
   (it got a truncated snapshot body, a leftover of reset_retry's phase 5). The acknowledgement
   bound is 500 ms, half release_session's 1,000 ms cap: a timed-out client closes about 1,000 ms
   after it connects, an acknowledging one within milliseconds of the answer.
4. 2026-11-08: `StateCursor::logical_commitment` and `PreparedReader::logical` keep their names
   on purpose: they return the one state commitment, predate Dense on main, and their callers sit
   in src/detent_delivery (outside this quest's files). A12's removed fields are the format's.
10. 2026-11-09: the b8ef8d2 held-poll thread and its close assertions stay. The thread is not what
   fixes the race (lazy snapshot is), but it keeps the fixture from depending on the coordinator
   retiring the poll socket before it opens the close; the assertions make a late close a
   failure instead of a silent pass. The rate comparison uses Detent:97f0f2f, not b8ef8d2:
   b8ef8d2's stricter assertions are a different test from main's; 2489dd (b8ef8d2 + logs)
   gives b8ef8d2's rate.

11. 2026-11-09: mutants and reverts run each in its own slot (`dense-mut-NAME`), not the shared
   dense-mutants slot, after observing three concurrent jobs in that one checkout (follow-up 2).
   Costs about 1.3 GB per slot; delete them after the batch.
12. 2026-11-09: merged quest/dense-pages--performance: it adds only the attribution probe
   (tests/detent_system/prepared_client_probe.cc) and its task journal, both in the file set.

13. 2026-11-09: the A15 gate runner (Detent:70377f2, c552990) is reverted on the branch
   (Detent:51a53a9, c421f8e): its jobs run at those commits, and leaving it out keeps the quest's
   code equal to Detent:435a525, where the whole evidence batch runs. If the lead wants the runner
   and its result doc to land with A15, revert c421f8e and 51a53a9; that changes the code and
   needs the batch again.

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

## Lead message 2026-11-09 (second 5M abort)

1. At Detent:98ebd27 both 5M jobs aborted with `invalid_argument: invalid paged local image`
   (5fdbec, 79f6d4). Second layer of the same defect: with the cursor and packer sized, the
   oracles' own structural checks (packer test `Sink::drain` verify_page and `verify_values`;
   delta oracle `Full::drain` verify_page and the occurrence walk) still used default limits, and
   `validate(PageHeader)` refuses `keys > 504,096` on upper 5M pages. Fixed Detent:ac4c32e: all
   take `corpus_limits`.
2. Detent:9770ca2 adds `report_uncaught_exceptions` (dense_fixture.hh): a terminate handler that
   prints type, message and the unwound-free backtrace. Diagnosis jobs at 9770ca2 (before the
   fix, expected to fail and to name the site; they are also the revert evidence):
   packer 261109-002829-1d2a73, delta 261109-002830-678fb1.
3. Batch at Detent:ac4c32e. A10: smoke 261109-002832-7724ff -> 5M 261109-002833-ba74d7.
   A3/A5/A7: 100K 261109-002834-b9ac85 -> 5M 261109-002836-c29925. Harness self-test
   261109-002837-05443e. Reverts (pass = killed): 261109-002839-00fc85, 261109-002840-cdfa5b.
   Mutants (pass = killed): a3-largest-first 261109-002842-4a14db, a3-seal-64
   261109-002844-c13209, a3-boundary 261109-002846-6b3239, a3-height-digest 261109-002847-06930c,
   a6-height 261109-002848-241da4, a6-keys 261109-002849-707844, a6-canonicity
   261109-002851-00afc7, a6-order 261109-002852-0fc85c, a11-noop-rewrite 261109-002853-0e22ed,
   a11-relocation-rehash 261109-002854-cecb99, a11-offpath-skip 261109-002855-28f894.
   Reruns of the brief's list: A12 grep 261109-002856-a757d2, 150551 -> 002856-89d2ac,
   150715 -> 002856-dd8697, 062527 -> 002856-d03cb7, 063837 -> 002857-957eef, 065403 ->
   002857-f63e1a, 070134 -> 002857-a75af3, 111601 -> 002857-9b0e92, 111645 -> 002857-d0560e,
   TSan 112739 -> 002857-169c6b, ASan 112740 -> 002857-cae590, warm smoke 113537 ->
   002857-5fbb95, COR-1 114739/114740/114741 -> 002857-8f8f5c / 002857-e50a4c / 002857-b0cd74,
   native 131622 -> 002857-48e472 (also `campaign verify`'s native check), ASan 131702 ->
   002857-79d264. The 98ebd27 queue was cancelled (stale test code).

## Session 2026-11-09 (default limits)

1. 261109-002833-ba74d7 (5M delta): `TrieOverlay::Impl::View::child` called
   `p::decode_external(bytes, slot)` with the default `Limits` (500,000 records + 4,096 namespaces),
   whose header check refuses the 5M root page (5,000,004+ keys) though the overlay was built with
   sized limits. Same omission in `PagedCursor::step` (page header) and `ReuseNavigator::step`.
   Production: a sparse update, cursor walk or reuse on any store above 504,096 keys (possible under
   `ViewLimits::prepared()`) failed. Fix Detent:791d9fc; revert patches Detent:65ae6de.
2. 261109-002857-957eef (frozen v2): `encode_generation_ownership` refuses layout version 2 at the
   head (ownership.cc:61, pinned by test_ownership.cc:79). Not the limits pattern; the job is valid
   only at Detent:785fb34 (ruling 7).
3. Mutant verdicts at Detent:ac4c32e (explicit harness, exit 0 = killed, none exit 3): all eleven
   killed (261109-002842-4a14db .. 261109-002855-28f894), both reverts killed, self-test
   261109-002837-05443e passed. 5M packer 261109-002836-c29925 passed (A5/A7).
4. Batch at Detent:c9db691, all after gate 261109-023907-3907fb (native): reverts cdb214, fe85d6;
   A11 mutants 80095a, d09b48, b86619; COR-1 16ed87, feea7a, 824414; A10 e56196 -> fd8f8f; model
   727b88 -> 5da14e; A12 15181a (no gate), 68e477 (no gate); self-test 51ac53; 89d2ac -> 3d7eea,
   dd8697 -> b0e5e3, d03cb7 -> d311c6, f63e1a -> 269318, a75af3 -> 39d01e, 9b0e92 -> b149d6,
   d0560e -> 2b1898, TSan 169c6b -> e3fdeb, ASan cae590 -> 041fbf, warm 5fbb95 -> 638ff5, ASan
   79d264 -> 5fbe88 (prefix 261109-0239/0240). Stale gate 261109-023744-fa8873 cancelled.

## Gate failure 2026-11-09 (msg from lead)

Native gate 261109-023907-3907fb at Detent:c9db691 failed: 174/179; the five 500K coordinator
subscription cases (#59, #69, #89, #90, #91) end with "fixture server: handshake: stream
truncated": the server's last expected accept (attempt 3 of 4) finds the coordinator already
closed after `feedback=received`. All 100K variants pass; at Detent:ac4c32e (48e472) all five
passed, run early in the suite. 24 dependents skipped.

Reading so far (not proof): the only production change on these paths is the overlay's limits,
which here are `paged_limits(ViewLimits{})` = default records/namespaces, the same bounds the
default argument gave; cursor and reuse moved to format limits (looser). #59 is a non-paged
mode. Hypothesis: load-timed, not 791d9fc. Attribution, identical commands, -j1, x2
(`--repeat until-fail:2`): before the fix Detent:c0020ff 261109-030629-e09f9f; head
Detent:c9db691 261109-030630-d655be.

Next: head fails and parent passes -> bisect the overlay/cursor/reuse change with the revert
patches; both pass -> requeue the gate (`campaign verify --force`) and the --after chain as
recorded above; both fail -> pre-existing, report with the c0020ff job.

Attribution result: 261109-030629-e09f9f at Detent:c0020ff (before 791d9fc) failed #69 once in
two serial runs, same truncation: pre-existing, intermittent, not the limits fix and not
parallel load. The failing run is shorter (27.4 s vs 42.9 s passing).

Reading of the race (to prove): the fixture server is one thread. In phase 2 it records
feedback, then blocks in `read_some` on the held long poll until the coordinator drops it; only
then can it accept attempt 3, the coordinator's final `release_session` (src/detent_delivery
coordinator.cc:820, timeout capped at 1,000 ms). If the coordinator keeps the poll connection
open past that second, the close request times out, the coordinator settles, the poll socket
goes with it, and the server accepts a dead connection: "stream truncated" 30 ms after settle.
Detent:9e87dde adds `server-feedback` and `server-poll-released` checkpoints; diagnosis
261109-042705-8e6377 runs #69 until-fail x8 with -V. If poll release lags feedback by over 1 s
in the failing run, the hold is the coordinator's (src/detent_delivery, outside this quest's
files): report, do not edit. A fixture-only fix (serve the held poll on its own thread) is in
scope but changes what the test proves; lead's call.

## Fixture fix 2026-11-09

Diagnosis 261109-042705-8e6377 (Detent:9e87dde) failed run 1: server-feedback 36,982 ms,
coordinator-settled 37,991 ms, server-poll-released 38,115 ms, then the fixture accepted the
coordinator's already-dropped close: "handshake: stream truncated". The coordinator's final
release_session (coordinator.cc:820, 1,000 ms cap) timed out while the one-thread fixture sat in
the held poll's read.

Fix Detent:b8ef8d2 (tests/detent_delivery/test_coordinator.cc): the held poll (phase 2; phase 4
in transitions) waits on its own thread, bounded by the source timeout; the accept loop answers
every session close with 204; the final request must be a session close; the coordinator must
close that connection within 500 ms of the answer; the answer must precede settling. Revert
patch `tests/detent_delivery/mutants/held-poll-inline.patch` runs the hold inline again.
Local sanity (quiet light, not evidence): all 25 non-500K test_delivery_coordinator cases pass
with the new assertions, including transitions, reset, cancel-open and worker-cancel.

Proof jobs, five 500K cases, `--repeat until-fail:8`, light:
1. 261109-044236-f5ae51 at Detent:97f0f2f (before the fix): expected to fail.
2. 261109-051245-db09c0 at Detent:b8ef8d2 (fix, -V): must pass.
3. 261109-051245-860eed (serial revert): cancelled before it ran, replaced by 6.
4. Serial f5ae51 passed its first 22 runs (#59, #69, #89 8/8 each, #90 6/6) at the time of
   writing: serial load rarely loses the race. The gate lost it under -j4, so the proof also runs
   the five cases in parallel: head 261109-052359-e76a0c (Detent:97f0f2f, must fail).
5. Fix in parallel: 261109-052359-b5065a (Detent:b8ef8d2, must pass).
6. Revert in parallel: 261109-052400-f703ba, `dense_mutant.sh` with held-poll-inline.patch and
   DENSE_MUTANT_RUN `ctest -j4 --repeat until-fail:8` on the five: passes when killed.

Requeued at Detent:b8ef8d2: gate (`campaign verify`) 261109-051101-0e5691; after it: reverts
844834, 5c50b4; A11 mutants 60ed43, 2bcd01, 8f3443; COR-1 ecbdb4, 43de33, 872c6e; A10 1b276f ->
d14e40; model 98ec25 -> 897c15; self-test a73aba; 3d7eea -> af6dae, b0e5e3 -> 92bc40, d311c6 ->
060c0c, 269318 -> 3fece6, 39d01e -> 598544, b149d6 -> 78b7fa, 2b1898 -> 6f8158, TSan e3fdeb ->
4655b8, ASan 041fbf -> 82ddbf, warm 638ff5 -> 941ad0, ASan 5fbe88 -> bb214e (prefix 261109-0512).
No gate: A12 15181a -> 525be9, 68e477 -> a204d2. Fuzz still waits for S1/S2 (ruling 5).
`campaign job rerun` drops `after`; these were resubmitted with their spec and the new gate.

## 500K round 3 2026-11-09

Reading of b8ef8d2's own logs (db09c0 serial, #59): server-feedback 16,071; poll released 16,899
and the close accepted 16,901 (handshake 16,903); coordinator settled 17,081; close answered
17,728. The settle is ~1,010 ms after feedback, i.e. after the observer's close (feedback + 6 ms):
release_session's 1,000 ms deadline, counted from its start, not from the close's connect. The
828 ms before poll-released and the 825 ms between handshake and answer are the fixture: for
every request it builds `response(count)` (500K records, tests/detent_delivery/test_coordinator.cc
`auto bytes = response(...)`) before choosing the reply, discarding it for the held poll and the
close. The held-poll thread is spawned only after that build, so server-poll-released marked the
build's end, not the coordinator's release. #89 db09c0: settled 37,848 before poll-released 37,918
and accept 37,920, then "stream truncated": same. 8e6377 (pre-fix #69): feedback 36,982, settled
37,991, poll released 38,115: same. The coordinator does not hold the poll: the earlier follow-up
was wrong (corrected below). b8ef8d2 made it fail more, not less: its "answered before settle"
assertion (line 1132) turns every late answer into a failure, and its close-acknowledged check
passed falsely on an already-closed socket.

Diagnosis Detent:00c9aaf (tests only): each accept logs peer port, attempt and bytes already
queued (FIONREAD), the request line, response build ms, held-poll end and error, handshake errors;
the observer logs its close. `tests/detent_delivery/repeat_500k.sh ROUNDS [BUILD]` runs the six
500K cases (the five plus relay-500000, which failed in gate 0e5691) ROUNDS times at -j4 without
stopping and prints failing case runs.

Fix Detent:203f729: the snapshot is built only for phases 1/5 and non-subscription modes.
Revert `tests/detent_delivery/mutants/snapshot-eager.patch` builds it for every request again.
held-poll-inline.patch removed (theory superseded; its job f703ba at b8ef8d2 is moot: cancel was
not permitted to me, the lead may cancel it).

Jobs, all light, -j4, -V, 6 rounds x 6 cases (36 case runs), fixed N, one command: `git show
792887c:tests/detent_delivery/repeat_500k.sh > .scratch/repeat_500k.sh && bash .scratch/repeat_500k.sh 6`:
1. 261109-063745-875b4f at main Detent:45e2044: rate on main.
2. 261109-063745-b2db8d at Detent:97f0f2f (quest code, fixture = main + one checkpoint): rate.
3. 261109-063746-2489dd at Detent:00c9aaf (b8ef8d2 + logs): timeline, before-rate.
4. 261109-063747-19ca42 at Detent:792887c (fix 203f729 + script fix): must pass.
5. 261109-063748-747eb5 revert at Detent:792887c, dense_mutant.sh + the script as DENSE_MUTANT_RUN:
   passes when killed.
Invalid, not evidence: 261109-063254-60ccf1 and 261109-063255-ce975b "done" in 0.5 s (zsh read
`$S:t` as a modifier, git show failed, bash ran an empty script); 261109-063256-8f7f5c exit 3
(ctest -N counts the TLS fixture: 7 not 6); f27cde, e99c35 cancelled (same script, which also
skipped rebuilding a reused slot's binary). Detent:792887c fixes both.
Requeued at Detent:d8f6cff (code = 203f729): gate (`campaign verify`) 261109-063352-da4860; the
chain rerun with the old gate in the set, so `after` maps to da4860: b02339, d4859c, 2d00f8,
bd758a, 7ed01d, 61b36a, 18eaab, 973d5d, c65072 -> 1908e9, f05d8f -> 0c4718, 79035a, 361de8,
da9795, 3f89dc, 6a25ab, d3ac7e, e3a875, b31605, 1b6a98, cdb818, 73121f, 78c3be (prefix
261109-0635, same order as the 0512 list above). No gate: A12 537286, 5a2ae9. The 261109-0634
batch skipped: rerun without the gate kept `after` on failed 0e5691.
Before-rate so far (not this job's): at b8ef8d2 every case failed its first run (db09c0 serial,
b5065a -j4); at 97f0f2f -j4 (e76a0c) every case failed within 6 runs. A pass of 36 runs at a
per-run rate p is luck with probability (1-p)^36: 2.6e-6 at p = 0.3, 0.023 at p = 0.1.

## Round-3 verdicts 2026-11-09 (fresh contributor)

1. 875b4f (main Detent:45e2044): exit 3, no rate. main's test_coordinator.cc does not compile
   with the script's configuration (Debug, DETENT_BUILD_RELAY=OFF): it gets asio::streambuf,
   read_until and buffers_begin only transitively through relay headers; main's own gate builds
   RELAY=ON (261109-121535-4154e6). Detent:97f0f2f added the four asio includes. No main rate.
2. b2db8d (Detent:97f0f2f, fixture = main's logic + includes + two checkpoints): failed, 2 of 6
   rounds, 7 of 36 case runs, all "fixture server: handshake: stream truncated" (#59 x2,
   migrate x2, paged, transitions, sessions). This is the before-rate; it stands in for main's.
3. 2489dd (Detent:00c9aaf, b8ef8d2 + logs): 36 of 36 failed at line 1161 (answer before settle).
   The timeline shows the reading in every run (parsed per port): settle - observer-close
   1,001-1,003 ms (median 1,002) = release_session's deadline; the close connection accepted
   695-957 ms after observer-close with pending=1572 (its request already queued while the
   server built the held poll's 500K response, build 693-1,006 ms); the close's own response
   build ended after settle in 36 of 36. The reading holds: the fix is proven, not just passing.
4. 19ca42 (Detent:792887c, fix 203f729): passed, 0 of 6 rounds, 0 of 36 case runs.
5. 747eb5 (snapshot-eager revert at 792887c): MUTANT KILLED, by the first ctest pass (6 of 6
   500K cases failed, "stream truncated"); DENSE_MUTANT_RUN not reached, by design.
6. Gate da4860 at Detent:d8f6cff: passed. Chain: all done/passed except 2d00f8 (a11-noop-rewrite)
   NO VERDICT exit 3, link "library 'libdetent_managed_agent.a' not found" while linking
   detent-agent (target-linked in src/CMakeLists.txt:87; other mutants built the same tree:
   transient, rerun at the merged head) and 1908e9 (A10 5M) cancelled (not by me): rerun at head.

## Merged head 2026-11-09

Detent:8eeb36e merges main (Detent:ff6218e, S1/S2 landed). One conflict, src/detent_local/
disk_store.cc StartupOperation: main replaced the census DirectorySync with SyncOpen
(storage.meta read-only) -> Barrier (SyncFiles, which also syncs the directory) -> SyncClose;
Dense added RemoveSelected (remove an obsolete v2 `selected`) -> DirectorySync -> credit. Merged:
obsolete: RemoveSelected -> SyncOpen -> Barrier -> credit the descriptor -> SyncClose -> ready;
otherwise main's path unchanged. The S1/S2 mutant strings still match once each
(mutant.sh startup-before-barrier, empty-payload-group, unsynced-descriptor, rename-before-sync,
closed-session). paged_publication_artifacts.cc, tests CMakeLists and
test_paged_publication_artifacts.cc merged clean (owners+lease first group; Dense's lease version
is paged::format_version). test_dense_refetch.cc crash cut 1 now expects the barrier's
storage.meta reopen after the removal (was DirectorySync). Docs: dense-pages.md (credit after the
startup barrier), sync-groups.md (restart diagram names the obsolete removal).
Detent:435a525 merges quest/dense-pages--performance (the attribution probe and its journal).

Jobs at Detent:435a525 (all light):
1. Gate `campaign verify`: 261109-145341-9f0788.
2. S1/S2 T7 positive `mutant.sh positive`: 261109-145355-7c2f59; `mutant.sh
   startup-before-barrier` (passes when killed): 261109-145403-0a4f36.
3. Rerun after the gate (rerun with da4860 in the set maps to 9f0788): A3/A5 100K fb0366 -> 5M
   933d09; overlay ASan cecb4a; A10 smoke a00ef8 -> 5M 28d1cf; TSan fe08c1; fullbuild ef7fac,
   02bdc4; five-fixture 37b414; overlay Debug 71a2c4; refetch 2af02e, 9b17ea; COR-1 parent kill
   bd9ad1; reader fixtures f3bd3f; ASan composed dc2bcc; 1,000 rounds 89ebfd; warm smoke c3f5af;
   overlay+corpus ASan 79fbd0 (prefix 261109-14575). No gate: A12 5a7ad1, 969274; fuzz (ruling 5,
   this is the post-S1/S2 head) codec 649826, warm smoke 2539d5 -> reader/graph/warm 976098.
4. Mutants and reverts, each in its own slot (`dense-mut-NAME`, see ruling 11), after 9f0788
   (prefix 261109-1458): a3 largest-first 3dfd88, seal-64 370f1a, boundary adf192, height-digest
   95cb9d; a6 height 90dc99, keys 33fefb, canonicity 953b99, order 2916b3; a10 key-grammar 036ceb,
   signed-order 9d2b50, overlay-default-limits f28cd1, cursor-default-limits 623329; a11
   noop-rewrite 7e3011, relocation-rehash 1a6698, offpath-skip 86a096; harness self-test 29b141.
   The a3 jobs name DENSE_CORPUS=/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-100k.bin.

## A15 baseline 2026-11-09

Question: why does the baseline arm of 261106-173358-3b82ed (Detent:a729b77, exclusive) miss
200/200 (90/200, median 1,031 us)? Compared with the records reader.md T2 cites, from the job's
result.json, its jsonl samples and the raw qualification record:

| Record | Publisher | Below 1 ms | Median first | attach | lookup | CPU | Build |
|---|---|---|---|---|---|---|---|
| qualification 2026-10-26 (Detent:0e862f0) | running | 200/200 | 715 us | 436 | 270 | 759 us | LLVM 23.1.2 Release, no counters |
| same | stopped | 198/200 | 806 us | 439 | 357 | 851 us | same |
| capsule trial 2026-10-29 | stopped | 198, 193/200 | 800 us | | | | |
| 3b82ed baseline (Detent:785fb34 binary) | stopped | 90/200 | 1,031 us | 578 | 428 | 1,076 us | AppleClang 17 Release, READ_COUNTERS=ON |
| 3b82ed candidate 5M | stopped | 115/200 | 974 us | 548 | 379 | 1,017 us | same |

Same: platform (macOS 15.8.1 x86_64, the same i9 host), resident data (files read before the arm,
0 major faults; minor faults 98 vs 85), warm process (a warmup process per key before each
measured one), clock (steady_clock from before Client construction to after the first checked
value), 40 keys x 5 rounds.
Different:
1. Publisher: the job runs only the stopped arm (dense_reader_measure.py hard-codes it). No record
   has ever met 200/200 stopped (198, 198, 193); only the running arm did. T2 requires both.
2. Toolchain: AppleClang 17 (/usr/bin/c++) vs Homebrew LLVM 23.1.2 (available at
   /usr/local/opt/llvm/bin/clang++).
3. DETENT_READ_COUNTERS=ON in both job binaries (added 2026-11-03, after both prior records).
4. Code: 785fb34 is 313 commits after 0e862f0.
5. The baseline binary was copied from a worktree build dir (dense-pages-baseline), not built in a
   commit-pinned job; only its hash is recorded.
6. API: T2 times `detent_open` through S6's C ABI; S6 is not on main, so the job times the
   pre-S6 `detent::Client`. reader.md:31 says today's path misses 1 ms in three of four arms: the
   200/200 gate is S6's target, not a property of the baseline.
The slowdown is CPU (process CPU +26% with wall +28%, attach +32%, lookup +20%), so 2-4 (or host
frequency) are the candidates; the job cannot separate them.
Recommendation (the lead's call, criterion unchanged): do not weaken the gate on 3b82ed. Rerun
the first-read arms with (a) publisher running and stopped, (b) counters OFF, (c) both binaries
built in commit-pinned jobs with the qualification's toolchain, interleaved under exclusive, with
an A/A pair. If the baseline then meets the running-arm 200/200 and the candidate does too, A15's
first-read part holds; if both miss only the stopped arm, that is the pre-existing condition
reader.md:31 records, and whether A15 waits for S6's T2 is the owner's/lead's ruling. On these
numbers the candidate is not worse than the baseline (974 vs 1,031 us median, 115 vs 90/200).

## A15 gate rerun 2026-11-09 (lead ruling: gate unchanged)

Runner Detent:70377f2, script fix Detent:c552990: `docs/perf/first-read-gate.md`,
`docs/perf/first-read-gate.sh`, `tests/detent_system/first_read_gate.py`,
`tests/detent_system/first_read_export.cc`.
1. Prepare 261109-151355-f406a9 (light, 150 min): Dense at Detent:70377f2, baseline main
   Detent:4d56489 (--ref BASELINE; its code equals ff6218e, the merged main). Homebrew LLVM 23.1.2
   pinned (fails on another version), Release -O3, DETENT_READ_COUNTERS=OFF, both checked in each
   CMake cache. Each tree exports the pinned 5M corpus (DENSE_CORPUS, sha256 recorded) as 1- and
   1,000-namespace stores with its own builder (layout 2 / 3) and prepares them with its own
   probe; builds its own detent-agent.
2. Measure 261109-151427-210773 (exclusive, 30 min, after f406a9): 10 arms x 200 samples:
   stopped B1 D1 D1000 B1000, running B1 D1 D1000 B1000, A/A D1 B1 stopped. Running arms start the
   tree's detent-agent over the arm's clone, upstream bound but not listening (as the 10-26
   qualification). An arm that cannot run is recorded with its error; the job exits 2 then.
   (261109-151412-b2d859 cancelled: the first script required DENSE_CORPUS to measure.)
Read path timed: the pre-S6 `detent::Client` (prepared_client_probe --read, first_live_ns,
steady_clock from before Client construction to the first checked value), not S6's `detent_open`.
Risk: the agent may refuse an exported store (cursor/scope are set to match managed.json:
cluster 63.., group 67.., epoch 65.., scope = canonical_scope_identity("system-reader", 1, names));
then the running arms carry an error and the stopped and A/A arms still stand.
Reader-core arm: possible from reader/s6 (= quest/detent/reader-s6, Detent:b1d281c, 187 commits
behind main) only over the baseline's version-2 stores: its C ABI (`include/detent/reader.h`,
detent_open/detent_get) exists, but `benchmarks/reader_bench.cc` times warm lookups only, so a
~40-line first-read probe built at that branch is needed. It cannot read Dense (version-3) stores
until S6 is rebased onto Dense. Not queued: the lead's call.
d4859c (revert a10-cursor-default-limits at d8f6cff) is contaminated, not evidence (follow-up 2);
rerun in its own slot: 261109-145822-623329.

## Follow-ups for the lead

1. Withdrawn: "the coordinator holds the long poll open while it releases the session". The
   poll-released checkpoint marked the end of the fixture's own response build (round 3 above).
   Remaining coordinator fact, not a defect claim: release_session's 1,000 ms cap counts from its
   start, so a relay that takes ~1 s to read a close leaves the session to expire.
2. Campaign tool: slot exclusivity failed for slot dense-mutants. Jobs 261109-063511-2d00f8
   (ran 634-798 s after 1794239000), 261109-063510-b02339 (606-799) and 261109-063510-d4859c
   (705-822) ran at once in one checkout: d4859c's log diff carries three patches (cursor, noop
   assembler, overlay), 2d00f8's two, and 2d00f8 failed to link (libdetent_managed_agent.a not
   found). d4859c's "killed" is contaminated; its rerun 623329 decides. Earlier verdicts from
   shared mutant slots are suspect wherever times overlap. Cause not found (slot_lock's pid check
   looks right on this host).
3. A15: see "A15 baseline 2026-11-09".
