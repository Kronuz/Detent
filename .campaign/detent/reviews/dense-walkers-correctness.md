# dense-walkers review: correctness

Commit: Detent:51535b08519255c7a7b455174094cfa9392b1edf
Verdict: APPROVE
Date: 2026-11-11

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261111-013720-736885 (A1): `decode_node(` appears in `src` only in `NodeAt::decode` and its
  definition.
- 261111-013747-1068a8 (A3): mutant dw-no-progress is killed by `test_local_dense_walkers`.
- 261111-013718-1582cb (native at 51535b0): 235 of 236 tests pass, every `test_local_*` among
  them. The one failure is `test_authority_snapshot_catchup`, an HTTPS read timeout in
  `test_multivoter.py:53`. The quest touches no authority code, and the same test also failed in
  native runs for namespace-s1-s2 (261109-085306-1a0b81) and daemon-ready-leader
  (261109-162029-7d42a7). See W3.
- 261111-012103-71570c: the fuzz smoke at 34c6318 (ASan/UBSan, 1.27M runs, no findings). I did
  not rerun it at this commit.
- 261111-013803-aa091e (A4 as submitted): I read its raw `runs.txt`. My own A/B probe re-reads its
  result (W1).

## What I checked myself

All probe scripts are in `.scratch/review-dense-walkers-correctness/`.

1. **Risk 1: every decode is reached through `NodeAt`.**
   - The `NodeAt` constructor is private. A position can come only from the default (the page's
     first node), from `after(node)` (`node.end`), or from `child(parent, slot)`, which refuses an
     external slot or `slot < parent.end` (`paged_node_step.hh:11-14`).
   - The stored offsets now hold a `NodeAt`, so no raw integer can be decoded:
     - `OverlayReference::at` (`paged_trie_overlay.hh:9`).
     - The walker frames in the cursor, graph validator and warm validator.
     - `ReuseNavigator::at`, and the packer's `injected_start` and `injected_position`.
     - `push_cluster(page, NodeAt, path)`.
   - `LazyPageAccess::resolve` now steps with `after` (`lazy_page_access.cc:211-218`).
   - Every `OverlayReference` built from a slot pairs `child(node, slot)` with that node's own page
     reference (`paged_trie_overlay.cc:31`).
   - The page-end half is `decode_node`'s
     `offset >= page_header_bytes && offset <= end && end - offset >= 3`, together with the
     per-kind body bounds (`paged_layout.cc:230-251`).
   - The packer's `InjectCopy` re-parse follows no slot (ruling 6). Each inline slot it rebases was
     already matched to the preorder position.
2. **Risk 2: `child` and `after` stay forward and terminate.**
   - Every inline step lands at or past `parent.end`, which is more than the parent's offset, so the
     offsets along any path strictly increase and no cycle is possible.
   - `after` advances by at least 3 bytes per node. The linear scan in `resolve` stops at
     `nodes_end`, and `decode_node` keeps `node.end <= nodes_end`.
   - Probe job 261111-020949-6ffb83 (`probe_page_end.cc`, run via `run.sh` with
     `REVIEW_PROBE_ONLY=1`) forges a verified page whose branch slots point at:
     - Exactly the node-region end (32760).
     - Two bytes short of it.
     - Three bytes short of it.
     - Past the page (0x7fff).
   - In all four cases `NodeAt::decode` throws `invalid_argument`, `PagedReadState::lookup` throws
     `ReadSourceFailure` for "KK", "WW" and "KKK", and `TrieOverlay` refuses. The job passed.
   - In the preorder validators, `child(frame.node, slot) == position` is equivalent to the old
     `slot == position`, because `position` is always past `parent.end`. Both failures are
     `invalid_argument`, so the validators' behavior is unchanged.
3. **Risk 3: canonical pages still pass the new back-edge refusal.** Every page the packer emits
   passes PageValidator's exact preorder check, so each inline slot is at or past its parent's end.
   The contributor's two 5M jobs, 261111-013732-376f7f and 261111-013739-bb67c4, failed without
   running the oracle (W2). I reran both at 51535b0 with the pinned corpus:
   - 261111-014803-79782f (5M packer and full build vs the model) passes with exact model pages and
     placements. Root `67c70ad3…`, 162,367 pages and 315,432,480 page bytes, identical to
     dense-pages' 261110-053631-a64f7f.
   - 261111-014809-74c269 (5M one-update and mixed-churn delta oracle) passes with exact model
     root, pages, page bytes, occurrence writes and off-path bytes. Root `bd67bd3a…`, 16,616 new
     pages and 99 off-path pages, identical to dense-pages' 261110-053631-a67982.
4. **Risk 4: the rewritten mutants target the same defects.**
   - a6-order still drops the whole preorder-position check in PageValidator. Only its spelling
     changed, from `slot == position` to `child(...) == position`.
   - a10-cursor-default-limits mutates the same `decode_page_header` line; only its context changed.
   - Probe job 261111-014716-7b2f76 (`run.sh`) runs each restated mutant against the test that
     killed its original, and all four are killed:

     | Mutant | Test | How it fails |
     |---|---|---|
     | a6-order | `test_local_paged_page_validator` | aborts, as in 261110-053632-d2c70c |
     | a10-cursor-default-limits | `test_local_paged_cursor` | aborts |
     | a10-overlay-default-limits | `test_local_dense_delta_assembly` | aborts |
     | cor1-unguarded-parent | `test_local_dense_overlay_bounds` | SEGFAULT |

   - That job exits 1 only because its page-end probe failed to configure. 6ffb83 above reruns the
     probe with that fixed.
5. **Risk 5: A4 cost and the A/A spread.**
   - aa091e reports lookup +13.67% (A/A +5.56%) and verify +13.03% (A/A +0.29%). Every one of the
     10 pairs is slower on both metrics.
   - `decode_node` is byte-identical in the two binaries but sits at a different address
     (0x14be0 vs 0x14c10). `verify_page_structure` differs by two calls on the cold throw path. The
     host is an i9-9980HK (Coffee Lake, affected by the JCC erratum).
   - Control: probe 261111-024754-d411c3 builds both trees with
     `-falign-functions=64 -mbranches-within-32B-boundaries`. Measure job 261111-024756-fe2855
     (`ab-control.sh`) then gives lookup +0.42% (A/A +1.46%) and verify +0.81% (A/A +1.28%).
   - So the step costs less than 1%, inside the noise. The +13% is code placement (W1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| W1 | no | gap | A4's only job, aa091e, shows a +13% regression on both metrics, far outside its A/A. Recorded as is, it either fails A4 or gets misread as "no regression". With placement controlled, the step costs +0.4% and +0.8%, within an A/A of about 1.3–1.5%. The A/A is also a single first-half-vs-second-half split of the baseline runs. It caught drift but cannot catch a layout effect shared by every run of one binary. | aa091e `runs.txt` (all 10 pairs slower); control d411c3 + fe2855; `nm` shows `decode_node` moved with identical code | In Result, report both A/Bs and their magnitudes, with fe2855 as the cost of the step and aa091e as the cost of placement. Tag the control pair for A4, or rerun it under the contributor's own job. |
| W2 | no | gap | Both A2 5M jobs were submitted without `--env DENSE_CORPUS=…/corpus-5m.bin`, so they exit before the oracle runs: 376f7f exits 1 and bb67c4 fails after its build. A2 has no passing model evidence of its own. | `campaign job log 261111-013732-376f7f`: `DENSE_CORPUS: pinned corpus path` | Resubmit with the env, as dense-pages' a64f7f and a67982 did. My reruns 79782f and 74c269 pass at this commit. |
| W3 | no | gap | Native at 51535b0 is red on `test_authority_snapshot_catchup` only: an HTTPS read timeout under `-j4`, in code the quest does not touch, and this test has failed in two other quests' native runs before. | 1582cb log; 261109-085306-1a0b81, 261109-162029-7d42a7 | `campaign job rerun` native. The flake belongs to whoever owns the authority tests. |
| W4 | no | nit | Contextual reuse and the assembler's corresponding-path walk now refuse back edges, but no forged-page test covers either. They rely on the shared `NodeAt::child` test and on 5M acceptance. | `test_dense_walkers.cc` covers read state, overlay and `NodeAt` only | Optional: add both walkers to `Forged::refuse`. |
| W5 | no | nit | A cursor progress failure is now `invalid_argument` instead of `ReadSourceFailure` (ruling 3). Its decode failures already were, and no caller tells the two apart. | `paged_cursor.cc:145`; no `catch` at the cursor's callers | None needed. |

## Not run

- The fuzz smoke at 51535b0. 71570c ran at 34c6318.
- Any sanitizer build, other platforms or CI.
- The 100K and synthetic model modes.
- An A/B on a non-x86 host.
- Contextual reuse with forged back-edge pages (W4).
