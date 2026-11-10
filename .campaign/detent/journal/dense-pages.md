# dense-pages: journal

State: waiting on jobs (final head Detent:531c370; ids in "Final head round 2026-11-09")
Quest: /Users/kronuz/code/Kronuz/Detent/.campaign/detent/quests/dense-pages.md

## Doing

Final head Detent:531c370: D42 design fixes, D43 single-format refusal, the widened file set's
DES-1/DES-9 edits (Detent:421ca64), the A15 runner with its sysroot fix (Detent:3442d64), and the
paged-recovery refusal expectation (Detent:941a17e), the repeat_500k round loop (Detent:531c370). Every evidence job reruns there:
"Final head round 2026-11-09".

## Next

1. Read the 531c370 batch into Evidence; fix any failure (a fix moves the head: rerun the batch).
2. A15: read prepare 261109-181435-2ca86d and halves 4150fc / 001cb2 against the gate.
3. Then State "ready for re-acceptance".

## Blocked on

1. D43 baggage outside this quest's set, routed by the lead to legacy step I: the flat v1 image,
   its migration and `legacy_anchor` paths, the no-metadata equal-cursor replacement, and the
   `paged-v2` config string (parsed in src/detent_delivery/configuration.cc and
   include/detent_delivery/coordinator.hh; ruling 27).

## For the lead's accepts (at Detent:531c370, pending until the batch passes)

- Design: DES-1 `stale_docs` 261109-181408-51c59f; DES-2 `reference_shape` 261109-181408-7cb320
  + des2-root-reserved 261109-181406-c207a8; DES-3 `delta_note` 261109-181408-55425a;
  DES-4/DES-9/DES-10 native 261109-181406-902906; DES-5 a3-largest-first 261109-181406-b40e93, a3-seal-64
  261109-181406-19113c; DES-8/DES-11 (D43) grep 261109-181408-e19361, refetch 261109-181406-f2bfa6,
  a12-no-refusal 261109-181407-08cc29, a12-no-barrier 261109-181407-924846.
- Correctness COR-1: Debug bounds 261109-181408-98bea7, ASan/UBSan 261109-181407-e55347, unguarded
  parent killed 261109-181409-45986b. PERF-1 is A15: prepare 261109-181435-2ca86d, stopped
  261109-181436-4150fc, running 261109-181437-001cb2.
- Security SEC-1: warm smoke 261109-181406-de2507, 261109-181406-d0413b; reader/page/graph/warm 3 seeds
  261109-181408-9e6a82. SEC-2 (one CPU-hour): codec 261109-181408-4a0809 + 976098's twin + top-up
  261109-181410-da309b; CPU totals to read from their logs.

## Evidence

Ids below are the measured runs at Detent:435a525 unless named; each reruns with the same command
at the final head Detent:531c370 (`campaign` takes the twin there), map in "Final head round
2026-11-09". Pending until that batch passes.

| Criterion | Evidence |
|---|---|
| A1 | Model reproduced by native at the head: 261109-145754-fb0366 (synthetic, 100K), 261109-145754-933d09 (5M), with `docs/design/dense-pages-model.py`. Design `docs/design/dense-pages.md` resolves DP-1..DP-6; contracts approved at Detent:7d339fe (`.campaign/detent/reviews/dense-pages-contracts.md`). The lead may tag A1 [artifact]. |
| A2 | Native gate 261109-145341-9f0788 (180/180 tests); full builder 261109-145755-ef7fac, 261109-145755-02bdc4 (segment, snapshot builder, packer, dense_fullbuild incl. empty admission). |
| A3 | 261109-145754-fb0366: synthetic 913 pages / 1,266,995 B, root 30a019eb...; 100K 3,245 pages / 6,209,298 B / 6 packs / 6,217,662 B, root 7c0f7d1b...; "exact model pages and placements". Mutants killed (exit 0 = killed): largest-first 261109-145809-3dfd88, seal-64 261109-145810-370f1a, boundary 261109-145812-adf192, height-digest 261109-145813-95cb9d. |
| A4 | Decoder fuzz, 0 findings: codec 261109-145755-649826 (726.9 CPU s), reader/page/graph/warm 261109-145755-976098 (2,820.25 CPU s), top-up 261109-163723-12ac7c (535.41 CPU s); total 4,082.6 CPU s, 0 crashes. Warm smoke 261109-145755-2539d5, 261109-145755-c3f5af. Corpus replay test_local_paged_codec_replay under ASan/UBSan 261109-145755-79fbd0. Hosted CI replay: open (A13). |
| A5 | 100K 261109-145754-fb0366 and 5M 261109-145754-933d09: native full build equals the model (5M 162,367 pages / 315,432,480 B / 302 packs / 315,759,038 B, root 67c70ad3...). |
| A6 | Validator mutants killed: height 261109-145814-90dc99, keys 261109-145815-33fefb, order 261109-145817-2916b3 (test_local_paged_page_validator), canonicity 261109-145816-953b99 (test_local_paged_graph_validator); suites pass in 261109-145341-9f0788. |
| A7 | 5M counters 261109-145754-933d09: page 162,367 / 306,989,416 B, pack 302 / 315,759,038 B, metadata 303 / 22,064 B; exact accounting and empty admission 261109-145755-02bdc4. |
| A8 | Reader fixtures and pressure 261109-145755-f3bd3f; native 261109-145341-9f0788. "Landed S6 interfaces are preserved": open while S6 is not on main (the lead's S6/Dense order). |
| A9 | Overlay bounds (COR-1) Debug 261109-145755-71a2c4, ASan/UBSan 261109-145754-cecb4a and 261109-145755-79fbd0, unguarded-parent patch killed 261109-145755-bd9ad1. S3b's bounds on main: open (S3b/Dense order). |
| A10 | 1,000 seeded trials of 1..500 changes equal full rebuilds 261109-145755-89ebfd. 100K real deltas 261109-145754-a00ef8: one update 5 pages / 14,276 B / 1 pack 14,287 B / 0 off-path; mixed churn 3,090 pages / 5,946,377 B / 6 packs / 5,954,604 B / 2 off-path 3,203 B; roots and occurrences equal the model. Repeated-content fixtures 261109-145755-37b414. Reverts killed: key grammar 261109-145819-036ceb, signed order 261109-145820-9d2b50, overlay limits 261109-145821-f28cd1, cursor limits 261109-145822-623329. 5M 261109-145754-28d1cf: one update 5 pages / 15,691 B / 1 pack 15,706 B / 0 off-path; mixed churn 16,616 pages / 42,821,363 B / 41 packs / 42,872,248 B / 99 off-path 220,903 B; all 5,000,004 bodies and occurrences equal the full rebuild and the model. |
| A11 | Compaction, delta plan, relocation fixtures 261109-145755-37b414; relocation zero page hashes in 261109-145755-89ebfd. Mutants killed: noop-rewrite 261109-145824-7e3011, relocation-rehash 261109-145825-1a6698, offpath-skip 261109-145826-86a096; harness self-test 261109-145827-29b141. |
| A12 | D43: one refusal path for every non-current layout. Refetch with the planner 261109-181406-f2bfa6 (test_dense_refetch `foreign_install`: layout-9 ownership over version-9 root and pages, refused and refetched, interrupted copy and restart included); refusal mutant killed 261109-181407-08cc29, barrier mutant killed 261109-181407-924846; no per-version recognition left: grep 261109-181408-e19361. The frozen v2 fixture and its job (5a7ad1) are removed (D43). Docs: `docs/mechanisms/dense-pages.md`, `docs/HOW_IT_WORKS.md`, `docs/mechanisms/verified-page-bitmap.md`, `docs/mechanisms/recovery-digest-once.md`. |
| A13 | ASan/UBSan 261109-145755-dc2bcc (10/10), 261109-145754-cecb4a, 261109-145755-79fbd0; TSan 261109-145754-fe08c1 (4/4); native incl. trusted restart and power cut 261109-145341-9f0788; revert/mutant batch in A3, A6, A9-A11. S1/S2 T7 positive at the head 261109-163620-dd9c24 passed (power_cut 5,766 cases in 121 s; paired main run 261109-163621-709877 passed in 45 s at the same time); startup-before-barrier killed at the head 261109-163623-0dd05f (power_cut invariant line 475 at 122 s, as on main; startup :97). CI at the final merged head: open. |
| A14 | Correctness CHANGES (COR-1 fixed, confirming jobs above; PERF-1 = A15); security CHANGES (SEC-1, SEC-2 jobs above); design review not launched; architect ruling none. Open. |
| A15 | Gate at the final head: prepare 261109-181435-2ca86d (light, baseline main = Detent:4d56489, code-equal to main 03e4781), measure stopped 261109-181436-4150fc and running 261109-181437-001cb2 (exclusive), pending. Earlier 6fcc50 failed (sysroot, ruling 24); 261106-173358-3b82ed missed the gate on both arms (see "A15 baseline"). Runner `docs/perf/first-read-gate.sh`, doc `docs/perf/first-read-gate.md`. |

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

14. 2026-11-09: the fixed runner (Detent:40b7a87) is again reverted (Detent:a288377), as in
   ruling 13: the A15 jobs run at 40b7a87 and the head's code stays equal to Detent:435a525.
15. 2026-11-09: SEC-2 counts measured CPU, not wall: 649826 got 726.9 CPU s of its 930 s under
   load, so the total was 3,547.2 CPU s. A 600 s codec top-up (12ac7c) at the head closes the hour
   rather than rerunning the whole hour.
16. 2026-11-09: the refetch barrier mutant (9b17ea) now cuts the merged startup: after
   RemoveSelected it calls ready() in place of `prepare(Phase::SyncOpen, c::Kind::OpenInto)`
   (main's barrier replaced Dense's DirectorySync); same kill check (ctest 8, "dense refetch
   invariant"). The refusal mutant is unchanged; 9b17ea already killed it.

17-19. Design round (D42): child_bytes admissions, StateCursor rename waits for coordinator.cc,
   which mutants rerun. Text under "Design fix round 2026-11-09".
20-23. D43 round: format identity is the ownership's layout value (a page in another framing
   under current ownership is fenced damage); v1 stays current (legacy H's); the model's
   `extract` stays as corpus provenance; the 80-byte body bound keeps its design reason.
   Text under "D43 round 2026-11-09".
24-27. Final head: the A15 runner stays in the head with the config file's SDK (supersedes 13-14);
   paged recovery expects the refusal path; superseded evidence; `paged-v2` stays outside the set.
   Text under "Final head round 2026-11-09".

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

## Resumed 2026-11-09 (fresh contributor)

1. 7c2f59 (T7 positive) failed by its 300 s wall bound, not a test failure: power_cut
   reached 1,965 cases at 232 s (window cuts 9.6 s and 13.8 s, then ~100 s per 252-case cycle
   step), startup and disk_store passed. The same HUNG at 300 s happened on main's ancestor
   Detent:35296ba (261109-121535-e93d52: window 1 cuts took 238 s), which passed in 26 s on
   rerun (76d6d1). Load at the time was 30-60 on 16 CPUs. Rerun dd9c24 at the head with a paired
   main run 709877 queued at the same time; if both hang, it is the pre-existing bound.
2. 0a4f36 (startup-before-barrier) is not a patch failure: the mutant applied and built; the
   harness's 125 is "HUNG, not evidence": power_cut hit 300 s after window 1 published, while
   test_paged_store_startup killed it (snapshot invariant at :97). On main the mutant dies at
   power_cut line 475 in 24 s (125239-3bf8a5). Rerun 0dd05f; if power_cut hangs again while
   the head positive passes, the hang is the mutant's (the Dense startup under no barrier) and I
   look at it before calling it killed.
3. 9b17ea: refusal mutant killed (ctest 8, invariant at 155/170); the barrier mutant's Python
   assert failed because the merge changed its target line (ruling 16). Replacement cc9df2.
4. A15 prepare f406a9 failed at the first cache check: CMake stores a -D compiler as
   `CMAKE_CXX_COMPILER:STRING=` or `:UNINITIALIZED=` (seen in the slot caches), never
   `:FILEPATH=`. Fixed at Detent:40b7a87: type-agnostic checks plus the detected version
   (CMakeFiles/*/CMakeCXXCompiler.cmake) and the build type; first_read_export.cc syntax-checks
   against the baseline's headers (clang 23.1.2 -fsyntax-only, exit 0). measure takes
   `stopped|running` so each half is its own exclusive hold (stopped 6 arms, 20 min bound;
   running 4 arms, 30 min, publisher readiness up to 300 s each). 3b82ed ran 4 arms x 200 in 70 s.

5. T7 pair: head dd9c24 and main 709877 both pass, so 7c2f59 was the wall bound under load.
   One paired sample had power_cut at 121 s (head) vs 45 s (main), same 5,766 cases; the native
   gate ran it in 27.6 s at the head (9f0788) and main has ranged 24-300 s. One light sample under
   load is not a measurement; if the lead wants the ratio, it needs a strict A/B.

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

## Design fix round 2026-11-09

Fixes (D42), at Detent:e879b9e / df7dabc / 5a134f9:

- DES-2 (a): the root's tree reference is the child entry, content [160,208) + placement
  [208,228), [228,320) zero; `decode_child` is the one decoder for page edges and the root;
  `encode_reference`/`decode_reference`/`reference_bytes` gone; validate-only callers use
  `paged::validate(const Reference &)`; root-prefix checks, exact fixtures (test_paged_layout:
  content/placement bytes, root bytes, root slice equals `encode_content`+`encode_placement`, every
  single byte of an empty root's 68 rejects), prefix tests and codec fuzz probe follow. The model
  encodes no v3 root (its `extract` reads DTROOT02), so it is unchanged. Corpus files stay (fuzzer
  inputs, none a valid root). New mutant des2-root-reserved.
- DES-1 (in set): HOW_IT_WORKS §3-5 (reference, first touch, warm lookup with the header compare,
  delta writes, compaction), read-counters.md:25, trusted-restart.md:66,70. Rest: Blocked on 1.
- DES-3: `docs/mechanisms/dense-pages.md` "Deltas and relocation"; HOW_IT_WORKS §3/§5 link it.
- DES-4: "Integration boundary" names each check and fuzz target.
- DES-5: `seal_eligible`/`seals_before` in paged_layout.hh; packer and page validator call them.
  a3-largest-first and a3-seal-64 now mutate the shared rule (the model check still kills a
  consistent change).
- DES-8: `obsolete_paged_layout(version)` at ownership.cc, generation.cc, disk_store.cc and both
  framing recognizers in paged_layout.cc.
- DES-9: `page_digests()` dropped (test_trusted_restart uses `hash_counts().page_calls`);
  `PreparedGeneration::state_commitment`, `Reader::state_commitment_` (private member),
  `expected_commitment`; `maximum_leaf_body_bytes` with its reason replaces `page_bytes - 80` in
  the overlay, delta leaves and packer test. `StateCursor::logical_commitment`: Blocked on 1.
- DES-10: design status IMPLEMENTED, Dense-first landing note, S6/S3b rows parked, S6 rebase
  points in D10, review table. A8's "landed S6 interfaces" wording is the quest file's (lead).
- Left: DES-6 (follow-up dense-walkers), DES-7 (reader-s6), DES-11 (owner).

Jobs at Detent:5a134f9 (light unless named; `.scratch/batch-5a134f9.txt`):

| Finding / criterion | Job | Replaces |
|---|---|---|
| DES-2 probe reference_shape.sh (none) | 261109-173524-f35dec exit 0 | b72512 |
| DES-3 probe delta_note.sh (none) | 261109-173526-3aaf82 exit 0 | 925d30 |
| DES-1 probe stale_docs.sh (none) | 261109-173528-ea4cb1 exit 1: 15 lines, all outside the set | c1bcc9 |
| native (A2, A13) | 261109-173756-e44822 (173355-9d4fa5 failed: my resubmit lost the shell) | 9f0788 |
| A3/A5 synthetic+100K model | 261109-173416-e0fb34 done: 3,245 pages / 6,209,298 B / 6 packs 6,217,662 B, root 7c0f7d1b..., exact model pages | fb0366 |
| A5/A7 5M model + counters | 261109-173418-9dea0d | 933d09 |
| A2 full builder | 261109-173419-b61b6b, 261109-173421-312c73 | ef7fac, 02bdc4 |
| A10 1,000 rounds + relocation 0 page hashes | 261109-173422-663f2c | 89ebfd |
| A10 100K / 5M real deltas | 261109-173424-a5e970, 261109-173426-8823d4 | a00ef8, 28d1cf |
| A10/A11 fixtures | 261109-173428-2785e4 | 37b414 |
| COR-1 / A9 overlay ASan UBSan, Debug | 261109-173430-084bbb, 261109-173445-252d5c | cecb4a, 71a2c4 |
| A4 corpus replay ASan UBSan | 261109-173432-c71c9a | 79fbd0 |
| A13 ASan UBSan | 261109-173434-da14e9 | dc2bcc |
| SEC-1 warm/reader/page/graph fuzz smoke ASan | 261109-173437-fb6421, 261109-173439-562ec5 | 2539d5, c3f5af |
| SEC-2 codec libFuzzer smoke, 300 s ASan UBSan | 261109-173511-af2ee8 | (649826, the hour) |
| A13 TSan | 261109-173442-587012 | fe08c1 |
| A8 reader fixtures | 261109-173447-5ca159 | f3bd3f |
| A12 refetch, refusal/barrier mutants | 261109-173450-19b800, 261109-173452-b3a34f | 2af02e, cc9df2 |
| A12 no live v2 references | 261109-173455-edbed0 | 969274 |
| Mutants (root encoding, shared rule, changed context) | a3-largest-first 261109-173512-6c24b0, a3-seal-64 261109-173514-a28416, a3-height-digest 261109-173516-b2b861, a6-height 261109-173518-814f93, a6-keys 261109-173519-e13ae9, a11-offpath-skip 261109-173521-5558ff, des2-root-reserved 261109-173522-cb07d0 | 3dfd88, 370f1a, 95cb9d, 90dc99, 33fefb, 86a096, new |

Rulings (design round):

17. 2026-11-09: per-reference memory admissions charge `child_bytes` (68) where they charged the
   65-byte `reference_bytes` (dependency walk, graph validator, overlay relocation): one size for
   one encoding. Budgets are admission costs, not wire bytes.
18. 2026-11-09: the StateCursor rename waits for coordinator.cc rather than keeping an alias; the
   in-set renames (prepared generation, reader member, finalizer) land now.
19. 2026-11-09: the mutants not touching the changed lines (a3-boundary, a6-order,
   a6-canonicity, a10-*, a11-noop-rewrite, a11-relocation-rehash, cor1) are not rerun: their
   patches apply unchanged and their code is unchanged. Rerun: every patch I edited, the new
   root mutant and a3-height-digest (paged_layout.cc).

## D43 round 2026-11-09 (lead msg: owner ruling D43)

Merged main (a303a7e, cf090a0) at c765cdf. At 77aebb4:

- One rule: a generation is current iff its authenticated ownership names a layout
  `current_layout` accepts (1, the flat image, or `paged::format_version`). Any other value is
  refused unread (`generation.cc`: `refuse()` -> `ObsoleteGeneration` -> remove `selected`,
  barrier, full copy). `decode_generation_ownership` accepts every layout value; encoding writes
  only current ones. The collector removes a non-current generation unread (no image or `.read`
  parse); `PagedNamedRoot` refuses one. The reader reports `GenerationUnavailable` for every
  non-paged layout (settles DES-11).
- Removed: DTPAGE02/DTROOT02 recognizers, `UnsupportedFormat`, `obsolete_paged_layout` (DES-8's
  predicate is moot), `validate_obsolete_root_prefix`, the frozen v2 fixture, its generator and
  check script. `verify_local_format.py` expects DTROOT03.
- `test_local_dense_refetch`: `foreign_install` builds a current store, then sets layout 9 in its
  ownership and version-9 framing in its root and every pack's first page, with every checksum
  matching. Restart cuts 0-3 (crash before removal, between removal and barrier, after absence,
  short replacement-root write) refuse and refetch; lease-held, foreign-file and damage cases
  (root vs checksum, storage, lease identity, current ownership over a non-current root,
  ownership vs `selected` checksum, unowned image, held owner.lock) fence or reclaim as before.
  test_paged_layout: roots and pages with version 2, 4, 9 are invalid. test_ownership: versions
  0, 2, 4, 0xffffffff decode, are not current and do not encode.
- Mutants a12-no-refusal and a12-no-barrier carried as patches (replace the inline cc9df2
  form, whose refusal target was the removed `catch (UnsupportedFormat)`).

First ddd1d24 run: 261109-174723-b5d26c failed (test_local_ownership expected a flipped layout
field to be rejected; foreign_install wrote no owner.lock, so the crash child exited 90). Fixed at
a904eef (tests only: `git diff ddd1d24 a904eef` touches test_dense_refetch.cc and test_ownership.cc), so
jobs at ddd1d24 that run neither test stand for the head; the rest rerun at a904eef.

| Finding / criterion | Job |
|---|---|
| DES-2 probe reference_shape.sh (none) | 261109-175748-7b7f6b exit 0 at a904eef |
| DES-3 probe delta_note.sh (none) | 261109-175749-970efa exit 0 at a904eef |
| DES-1 probe stale_docs.sh (none) | 261109-175749-b2d21e exit 1 at a904eef: the 15 out-of-set lines only (Blocked on 1) |
| A12/D43 grep (none) | 261109-175750-67c09e exit 0 at a904eef |
| native (A2, A12, A13) | 261109-175724-617306 at a904eef |
| A12 refetch with the planner, ownership | 261109-175730-ce4c49 at a904eef |
| A13 ASan UBSan (incl. dense_refetch) | 261109-175726-ce1145 at a904eef |
| SEC-1 reader/page/graph/warm fuzz smoke ASan | 261109-175727-e6b831, 261109-175728-c076bd at a904eef |
| A13 TSan (incl. dense_refetch) | 261109-175729-4c0bc7 at a904eef |
| Mutants at a904eef | a3-largest-first 261109-175731-2ec073, a3-seal-64 261109-175731-53edcf, a3-height-digest 261109-175732-2eb317, des2-root-reserved 261109-175733-6f5e1c, a6-height 261109-175733-9fca9d, a6-keys 261109-175734-d0fbd9, a11-offpath-skip 261109-175735-6f101d, a12-no-refusal 261109-175736-8385ca, a12-no-barrier 261109-175736-aa9863 |
| A3/A5 synthetic+100K (done), 5M + A7 | 261109-174653-689151 exit 0, 261109-174802-890ff1 at ddd1d24 |
| A2 full builder | 261109-174656-246235 exit 0, 261109-174806-3b102b at ddd1d24 |
| A10 1,000 rounds, relocation 0 page hashes | 261109-174658-180c13 at ddd1d24 |
| A10 100K (done) / 5M real deltas | 261109-174700-0521e3 exit 0, 261109-174810-102843 at ddd1d24 |
| A10/A11 fixtures | 261109-174704-4702b7 exit 0 at ddd1d24 |
| COR-1/A9 ASan UBSan, Debug | 261109-174705-1a49ef exit 0, 261109-174719-dddba9 exit 0 at ddd1d24 |
| A4 corpus replay ASan UBSan | 261109-174813-04dd4d at ddd1d24 |
| SEC-2 codec libFuzzer 300 s ASan UBSan | 261109-174848-e28eba at ddd1d24 |
| A8 reader fixtures | 261109-174721-6b20e3 exit 0 at ddd1d24 |

Superseded (cancelled or at 5a134f9): every job in "Design fix round" except its probes; at ddd1d24
native 7c1977, ASan 04a16e, TSan 80dd4c, warm fuzz d41ca2/eb92c5 and all ddd1d24 mutants were
cancelled for the test fix;
cancelled ids 261109-174654-732d9b, 174657-1b4841, 174702-8f31ee, 174707-dea400 had wrong
`--after` links and were resubmitted.

Rulings (D43 round):

20. 2026-11-09: format identity is the authenticated ownership's layout value only. A page or root
   cannot name its format, so under current ownership bytes in another framing are damage: fenced,
   never refetched (`embedded` case). Deletion authority comes only from an authenticated claim.
   The "non-current root and page refused and refetched" case is `foreign_install` (layout-9
   ownership over version-9 root and pages); the lead may overrule if a page alone must refetch.
21. 2026-11-09: v1 (flat image) stays a current layout: it is written today and its removal is
   legacy H's. Not removed (Blocked on 3): flat migration, the no-metadata equal-cursor
   replacement, the `paged-v2` config string.
22. 2026-11-09: the model's `extract` reads DTROOT02 stores: it is the pinned corpora's
   provenance, not runtime compatibility, so it stays.
23. 2026-11-09: `maximum_leaf_body_bytes` keeps page_bytes - 80 (design D2: a lone leaf fits a
   page); its comment no longer cites DTPAGE02.

## Final head round 2026-11-09 (lead msg: file set widened, A15 mine)

1. Merged main 03e4781 (file set widened); applied the DES-1/DES-9 patch: Detent:421ca64
   (stale_docs.sh exits 0 locally).
2. Native 261109-175724-617306 at a904eef failed test_local_paged_recovery: a layout-4 lease now
   decodes and takes the refusal path (ObsoleteGeneration), the test expected invalid_argument.
   Fixed at Detent:941a17e (ruling 25).
3. A15 prepare 6fcc50 root cause, reproduced with a 6-line CMake project
   (`.scratch/a15/`): Homebrew LLVM 23.1.2's config file sets `-isysroot MacOSX15.sdk`, so CMake's
   implicit include dirs are under MacOSX15.sdk; the runner passed `CMAKE_OSX_SYSROOT=MacOSX.sdk`
   (-> 26.2), so `find_package(ZLIB)` returned MacOSX.sdk/usr/include, unfiltered, and the
   imported target put it in as -isystem ahead of libc++ ("<cstring> tried including <string.h>").
   With MacOSX.sdk the probe fails with that error; with MacOSX15.sdk it builds. Fix
   Detent:3442d64: the sysroot comes from the config file and each cache is checked for it.
4. Cancelled every queued/running job at ddd1d24 and a904eef (code moved). 605b54/0e4b31 had
   already been skipped.
5. Lead msg (repeat_500k BSD `seq 0`): Detent:531c370 loops with an arithmetic for (fixture-500k's
   5c9403c); zero rounds now runs none (checked in a shell). The 941a17e batch was cancelled and
   rerun at 531c370 so every job runs the final code.

Batch at Detent:531c370 (old -> new; old ids cited in Evidence resolve to these):

| Old | New at 531c370 |
|---|---|
| 261109-175724-617306 | 261109-181406-902906 |
| 261109-175730-ce4c49 | 261109-181406-f2bfa6 |
| 261109-175726-ce1145 | 261109-181406-fa5fd7 |
| 261109-175727-e6b831 | 261109-181406-de2507 |
| 261109-175728-c076bd | 261109-181406-d0413b |
| 261109-175729-4c0bc7 | 261109-181406-96e215 |
| 261109-175731-2ec073 | 261109-181406-b40e93 |
| 261109-175731-53edcf | 261109-181406-19113c |
| 261109-175732-2eb317 | 261109-181406-7de097 |
| 261109-175733-6f5e1c | 261109-181406-c207a8 |
| 261109-175733-9fca9d | 261109-181407-c836ed |
| 261109-175734-d0fbd9 | 261109-181407-135d6f |
| 261109-175735-6f101d | 261109-181407-17019c |
| 261109-175736-8385ca | 261109-181407-08cc29 |
| 261109-175736-aa9863 | 261109-181407-924846 |
| 261109-174653-689151 | 261109-181407-e601a2 |
| 261109-174802-890ff1 | 261109-181407-af0918 |
| 261109-174656-246235 | 261109-181407-4ad379 |
| 261109-174806-3b102b | 261109-181407-d5faf2 |
| 261109-174658-180c13 | 261109-181407-eb9269 |
| 261109-174700-0521e3 | 261109-181407-1886db |
| 261109-174810-102843 | 261109-181407-f4a355 |
| 261109-174704-4702b7 | 261109-181407-0d850b |
| 261109-174705-1a49ef | 261109-181407-e55347 |
| 261109-174719-dddba9 | 261109-181408-98bea7 |
| 261109-174813-04dd4d | 261109-181408-fcb852 |
| 261109-174848-e28eba | 261109-181408-0f7526 |
| 261109-174721-6b20e3 | 261109-181408-b3d383 |
| 261109-175748-7b7f6b | 261109-181408-7cb320 |
| 261109-175749-970efa | 261109-181408-55425a |
| 261109-175749-b2d21e | 261109-181408-51c59f |
| 261109-175750-67c09e | 261109-181408-e19361 |
| 261109-145341-9f0788 | 261109-181408-2cceb8 |
| 261109-145755-649826 | 261109-181408-4a0809 |
| 261109-145755-02bdc4 | 261109-181408-7a32a4 |
| 261109-145755-ef7fac | 261109-181408-79144f |
| 261109-145755-976098 | 261109-181408-9e6a82 |
| 261109-145755-bd9ad1 | 261109-181409-45986b |
| 261109-145812-adf192 | 261109-181409-d36b74 |
| 261109-145816-953b99 | 261109-181409-cdd9fc |
| 261109-145817-2916b3 | 261109-181409-ec4c07 |
| 261109-145819-036ceb | 261109-181409-acc5ad |
| 261109-145820-9d2b50 | 261109-181409-caaba8 |
| 261109-145821-f28cd1 | 261109-181409-b5b4c9 |
| 261109-145822-623329 | 261109-181409-6f85dc |
| 261109-145824-7e3011 | 261109-181409-3ccea8 |
| 261109-145825-1a6698 | 261109-181409-ed863b |
| 261109-145827-29b141 | 261109-181409-6a5924 |
| 261109-163620-dd9c24 | 261109-181409-52d139 |
| 261109-163623-0dd05f | 261109-181409-c0e711 |
| 261109-163723-12ac7c | 261109-181410-da309b |

A15: prepare 261109-181435-2ca86d (light), stopped 261109-181436-4150fc, running
261109-181437-001cb2 (exclusive, after prepare).

Not rerun, superseded: cc9df2 (mutants of the removed UnsupportedFormat catch; replaced by
a12-no-refusal/a12-no-barrier), 5a7ad1 (frozen v2 fixture, deleted by D43), 969274 (v2-name grep;
replaced by the D43 grep), 709877 (main's paired T7 run, main's code unchanged).

Rulings (final head):

24. 2026-11-09: the A15 runner (docs/perf/first-read-gate.sh, .md,
   tests/detent_system/first_read_export.cc, first_read_gate.py) stays in the head, per the lead;
   supersedes rulings 13-14. Its sysroot is the SDK the LLVM config file names, not a fixed path.
25. 2026-11-09: test_paged_recovery's unsupported-layout case expects
   `detail::ObsoleteGeneration` from `GenerationReader::refresh` (the one refusal path) and still
   checks the installed view is kept.
26. 2026-11-09: evidence superseded by D43 is not rerun (list above).
27. 2026-11-09: the `paged-v2` config string stays: its parser and enum are in
   src/detent_delivery/configuration.cc and include/detent_delivery/coordinator.hh, outside the
   set; the tests that write it follow the parser. Lead routes it to legacy step I.

Probes at Detent:531c370, done: reference_shape 261109-181408-7cb320 exit 0, delta_note
261109-181408-55425a exit 0, stale_docs 261109-181408-51c59f exit 0 (DES-1 closed), D43 grep
261109-181408-e19361 exit 0. The rest of the batch is queued or running on a loaded host.
