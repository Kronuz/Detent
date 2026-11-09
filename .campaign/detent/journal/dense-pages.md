# dense-pages: journal

State: waiting on jobs 261108-213453-4048a8 261108-213503-22cf8f 261108-213503-7538e7 261108-214256-fab375 261108-214256-99a747 and the eleven mutant jobs listed under "Session 2026-11-08"
Quest: /Users/kronuz/code/Kronuz/Detent/.campaign/detent/quests/dense-pages.md

## Doing

A10 fixed (harness, not the read path); mutants for A3/A6/A11 and head reruns queued.

## Next

1. Read the A10 smoke/5M/revert jobs; record T4 actuals against the model projections.
2. Read the eleven mutant jobs; a survivor gets a test (or an equivalence argument) and a rerun.
3. Rerun the remaining evidence jobs at the head; rerun COR-1's three confirming jobs.
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
| A3 | 261105-150743-b66bbc (100K) and 261105-150744-5b7a4c (5M) equal the model exactly. Sealing-order, boundary and height mutant kills: only the pre-campaign "four packing mutants fail" (Detent:8b041fa); open at this code. |
| A4 | Codec libFuzzer 261106-112828-869301 (927.89 CPU s) and reader/page/graph/warm fuzz 261106-113725-c6d946 (3 x 940 CPU s): 3,747.9 CPU s, zero findings; warm smoke 261106-113537-ede7c2. Corpus replays in the native suite; hosted CI not run: open. |
| A5 | 261105-150743-b66bbc and 261105-150744-5b7a4c: 162,367 pages, 315,432,480 page bytes, 302 packs, 315,759,038 pack bytes, root and placements equal the model. Rerun at head. |
| A6 | Forged summary refusal in 261106-111645-b25734 and the native suite. False-height, keys, canonicity and order mutant kills not recorded: open. |
| A7 | 261105-150715-4c5b2d; 5M hash counts page 162,367/306,989,416 B, pack 302/315,759,038 B, metadata 303/22,064 B (261105-150744-5b7a4c). Rerun at head. |
| A8 | Reader fixtures 261106-111645-b25734 and the native suite pass. "Landed S6 interfaces are preserved" cannot hold while S6 is parked: open, the lead's S6/Dense order. |
| A9 | Overlay bounds (COR-1): 261106-114739-ba4d50 Debug, 261106-114740-c11743 ASan/UBSan, 261106-114741-2f126a kills the unguarded parent. S3b's bounds are not on main: open, the S3b/Dense order. |
| A10 | Typed 1,000-round oracle 261106-111601-989a59 passes. Real write targets: smoke 261106-173308-d6523a failed, exit 134, `ReadSourceFailure: local page walk failed` on the first synthetic one-update case; 5M job 261106-173308-538815 skipped. Open: a harness or read-path defect, undiagnosed. |
| A11 | Compaction/relocation fixtures 261106-062527-75b336 pass. Off-path, no-op rewrite and relocation mutant kills not recorded: open. |
| A12 | Frozen v2 fixture 261106-063837-22f097, refetch/ownership 261106-065403-f3c14e, refusal and barrier mutants killed with restored pass 261106-070134-ecbe03. "No live callers" and the mechanism docs: open, unchecked. |
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
