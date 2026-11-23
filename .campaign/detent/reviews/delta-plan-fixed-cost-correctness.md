# delta-plan-fixed-cost review: correctness

Commit: Detent:ae9a25456ecb18247e8e82b29c3bf088dab1a181
Verdict: APPROVE
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- `git diff 0b759d10 ae9a2545 -- src tests` and `git diff bd51ade2 ae9a2545 -- src docs/perf/namespace-measure-t9.cc`
  are empty, so jobs at Detent:0b759d10 and Detent:bd51ade2 ran this commit's code.
- Suite 261117-121549-83f289 (Detent:0b759d10): 268/268, one skipped (test_local_trusted_paths). Every
  existing warm-validator rejection test passes unchanged; no existing test was edited.
- Mutant 261117-121549-7a86f2 (byte comparison dropped): killed, at test line 198.
- Probe 261117-121529-6da30a: 2 leaf reads per changed key at 100K and 1M (950,000 moved unread at 1M);
  fails-without 261117-121537-35314f (`moved()` disabled, Detent:268feced): 40 per key, 100,000 validated.
- A3 261117-094340-baa902 (Detent:bd51ade2 against main Detent:186d5f1d, whose src equals the merge base
  d1987783's): every published file identical at 100K (0.05, c0.05, 0.5) and 1M (0.05, c0.05). The script
  hashes every store file but owner.lock. Both sides use one harness built against each side's library.
- A2 261117-094335-a15eaa and 5M 261117-094351-8b7ee4: I read the logs and recomputed the doc's rows.

## What I checked myself

Read through the code (paged_warm_validator.cc, paged_trie_overlay.cc, paged_navigation.hh, verified_pages.hh,
generation.cc's warm recovery, sparse_plan_finalizer.cc):

1. Skipping moved definitions is safe. `moved()` requires a Definition on both sides, the full key equal byte for
   byte, and the body equal byte for byte. No hash is involved. A definition's meaning is its key and body:
   depth, parent and sibling order are encoded in the key path, and the new walk still checks them
   (PageValidator, label order, key counts). The old leaf is on a removed base page that `checked_page_bytes`
   read from the base mapping. That page is digest-verified on first touch, or covered by a pack hash, and
   chains to a base root that was validated or trusted by its build. So the new bytes equal a definition
   that was already validated. Context changes are covered too:
   - namespace records are never matched, and their revision must stay >= the original
     (`validate_definition` checks `revision <= ns`, which is monotone);
   - the namespace set is fixed (`root.namespaces == prior.namespaces`);
   - per-definition `semantics.overrides` is enforced again for the whole generation by the Histogram
     phase (`maximum_overrides <= semantics.overrides`) and strings by Finalize, so warm recovery under
     tighter limits still rejects.

   A key match with different bytes falls back to full validation, and the test rejects revision 99. The
   old walk is interleaved, and every unmatched leaf is still counted on its own side. The worst ordering
   error would reject a valid graph (`frontier == frontiers.size()`), never accept a bad one; the suite and A3
   show it does not happen. Re-entry after budget exhaustion is idempotent. The test exercises it with
   {1,1} slices.
2. The `checked_page_bytes` fast path is keyed safely. The bit is per mapping (`SegmentInput.pages` and
   `bytes` come from the same `SegmentMapping`, segment_registry.cc:29) and per exact offset (a nibble
   code per 80-byte granule). The fast path still compares segment, length, keys, height and digest against
   the header bytes verified at that offset. Header offsets 8/16/24/32 match `decode_page_header`. A
   remap gets a new VerifiedPages. It trusts nothing that skipping `verify_page` did not already trust.
3. TrieOverlay: `frames[].view` is the view of `frames[].parent`, taken on descent. Parents are old,
   immutable bytes, and fresh parents ignore the view in `convert`. `keys` is used only after `counted()`
   (Convert, relocation). A3 shows the output is unchanged.
4. The T9 port's drive loop matches the suite's fixtures (test_paged_disk_store.cc:37).

Probes, queued at review time (scripts in `.scratch/review-delta-plan-fixed-cost-correctness/` in the home
checkout; results not yet in):
- 261117-123637-c7ad57 `reject_mutant.sh`: applies the byte-comparison mutant, removes the clean-count check
  at line 198, and passes only if `check(rejected)` kills the mutant.
- 261117-123641-911a1f `ab100k.sh`: 11 interleaved rounds of 100K uniform f 0.05 plan CPU, fix against main.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | gap | The mutant is killed by the clean count check (test line 198), not by the corrupt-definition rejection (line 210). The suite does not yet show that the safety assertion catches a same-key, different-bytes definition on its own. | 7a86f2 log "invariant at line 198"; probe c7ad57 (queued) | If c7ad57 fails, split the corrupt case into its own mutant run; if it passes, cite it. |
| C2 | no | gap | 100K uniform f 0.05 is slower with the fix in 4 of 6 samples: median -20.3% in a15eaa (pairs 1.34, 1.34, 0.89, 0.99, 1.20), and 0.391 s against 0.328 s in baa902. The doc calls this "within noise" without showing that it is noise. | a15eaa, baa902 logs; probe 911a1f (queued) | Cite 911a1f's result in the doc row; if it shows a regression, state it beside the 1M cut. |
| C3 | no | nit | "At 5M a delta now applies in 0.88x the full copy (0.99x before)" (doc, D110) mostly reflects the full arm's apply time moving from 14.50 to 15.83 s between rounds of different main code. The delta's own apply went from 14.38 to 13.95 s (3%; apply CPU 12.14 to 11.72 s). | fefadb, 8b7ee4 logs | Lead the sentence with the delta's own 3%. |
| C4 | no | nit | The 1M uniform 4.1% is a median whose ranges overlap (worst against best -2.7%, n=3). | a15eaa digest | State it as "about 4%, within the run range". |
| C5 | no | nit | A4's probe counts only the validator's leaf reads. A uniform delta's re-pack still scales with the store (1,001 to 10,006 pages written), which D107 reserves. The doc says so; A4's wording ("the fixed part no longer grows") is broader than the probe. | 6da30a output | Lead's call: restate A4 next to A2 (D110). |
| C6 | no | nit | The journal's Next says the suite 83f289 is "still running"; it passed 268/268. `campaign status` lists A1 and A3 as pending on freshness (code-identical; see above). | journal; `campaign status` | Update Next; `campaign job rerun` or record the code identity. |

## Not run

Warm recovery of a generation written by another process (generation.cc:424, own_build=false) with a moved
corrupt definition; reasoned above, not probed. Sanitizers (CI's). Results of c7ad57 and 911a1f were not
available when this file was written.
