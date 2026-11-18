# namespace-s4 review: correctness

Commit: 96854bda3b9132306570fa8f0bb1d674281c736a
Verdict: CHANGES
Date: 2026-11-11

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

All at 4452eee0; 96854bda only merges main, whose delta touches `src/detent_authority/service.cc` and
authority tests, nothing in local, delivery, distribution or relay (`git diff 4452eee0 96854bda --stat`).

- 261111-092439-33747b native suite: green, including `test_delivery_hash_once_{agent,relay,agent-delta,restart,damage}`,
  `test_managed_recovery_failure`, `test_state_equivalence` and the repair modes.
- A1 mutants, each read in its log, each killed by the intended assertion, not a crash or timeout:
  0ed21e (`revalidate-own-build`) and b0bb5b (`candidate-rehashes`) fail `agent` at line 50 with
  hashed page/pack = 692,660/703,742 against built 346,330/351,871 (exactly one extra pass each);
  e582da (`delta-candidate-rehashes`) fails `agent-delta` at line 107 (75,494/76,558 against 38,755/38,279).
- A3 mutant e23830 (`scrub-no-compare`) fails `damage` at line 181 after 121 s: no repair without the
  scrub's comparison, so the scrub is the detector. A3 test a6e823 passes `damage` and `restart`.
- Damage stress 184ad3 at 4452eee0: 0 of 160 runs failed, 8 at once.
- A2 99825c (strict): hashed page and pack bytes equal built bytes in every row (ratio 1.000) at 10K and 100K,
  agent, relay, agent-delta and relay-delta; with frames, agent 1.094/1.108, relay 1.383/1.441 (D64).

## What I checked myself

Read the whole quest diff (`git diff main...96854bda -- src include tests`) against the questions.

1. Own-build bytes trusted after they changed on disk. Packs are created `O_CREAT|O_EXCL`
   (`completion/posix.h:756`) under ids derived from a `getentropy` generation id
   (`paged_segment_builder.cc:39`, `disk_store.cc:43`), and written from the same sealed buffer the
   `SegmentBuilder` hashed (`paged_publication_artifacts.cc:80`), so no leftover or torn file of an earlier
   attempt can be adopted. A crash anywhere before `selected` leads to a cold start, which is S3's path
   unchanged (`generation.cc:384` with no admission and no prior yields `TrustedOwn{}`: fresh registry,
   `require_first_touch` on every pack, byte-identical to main). The old post-publication re-hash read the
   packs through the page cache moments after the write, so it caught only an external writer in that
   window; the old recovery then left these packs without first-touch bits too (the discovery branch,
   unchanged, never calls `require_first_touch`). Damage after the window was the scrub's before and is the
   scrub's now; e23830 shows the scrub is what catches it. The per-segment loop (`generation.cc:492-506`)
   still checks every `.owner` record against the mapping's ownership and the named file's inode, size,
   owner, mode and link count, carried prior mappings included.
2. `TrustedRestart::open` after a publication. In process it reads `.read`/`.live` the publication just
   staged and renamed; both are bound to the root checksum, which is still hashed, and to each other.
   With an admission every new row goes through `take_segment`, which requires ownership (id, length,
   checksum) equal to the plan's reservation (`mapped_recovery_admission.cc:68-76`), so `.live` cannot
   introduce or alter a pack the build did not make; a prior row must equal the prior's ownership
   (`trusted_restart.cc:118`). A wrong or missing record throws, which fences and closes the store
   (`settle_failure`): fail-closed. Progress publications keep their old path.
3. Counters. Only two sites call `count_build_hash` (packer page digest, segment checksum); every
   re-validation site calls `count_hash`, so `hashed - built` is the re-validation and the mutants move it
   by exactly one built-size pass. Frame counting adds one call per `hash.add` in encoder, `encode_fragment`
   and decoder, with no path through two of them for one fragment. One defect in the composite read: see F1.
4. Frame SHA-256: the `framing.cc` and `frame_encoder.cc` hunks only add `count_frame_hash` after an
   unchanged `hash.add`; no hash input, header or byte changed.
5. Tests: the mutants kill for the right reason (above). The damage test reads only counters that grow,
   with a fixed seed; 184ad3 is the evidence for determinism.

Probe 261111-105501-55ce09: `.scratch/review-namespace-s4-correctness/counter_tear.sh` builds
`detent_local_codec` and runs `counter_tear.cc`, one thread counting build hashes while another reads
`delivery::hash_stats()`; exit 1 means a torn read (checked_bytes nonzero though nothing but builds hashed).
Result at 96854bda: exit 1, `reads=25455905 torn=3822927 worst_checked_bytes=18446744073709547616`
(15% of reads torn; the worst is 2^64 - 4000, one page's bytes wrapped).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | `hash_stats()` reads the totals before the built counts while `count_build_hash` adds to the totals first, so a read during a build can see built > its share of the totals and `checked_bytes()` wraps to near 2^64 in `/v1/health` (`hash_checked_bytes`) or the agent log. At steady state checked is a few hundred bytes (392 at 10K) and one pack adds ~350 KB, so any overlap wraps. The tests read after builds settle, so A2's measured ratios stand; the exposed counter does not. | `include/detent_delivery/hash_stats.hh:14-26`, `src/detent_local/paged_layout.cc:122-147`; probe 261111-105501-55ce09 | Read `built_hash_counts()` before `hash_counts()` with acquire loads and release adds (clamping alone hides the tear in `total`); add a suite test shaped like `counter_tear.cc` (writer thread on `count_build_hash`, reader asserting `checked_bytes() == 0`) and rerun probe 261111-105501-55ce09's script. |
| F2 | no | gap | The counters do not count every SHA-256 the tier runs, though `CoordinatorStatus::hash` says "Everything this process has hashed": `PackScrub` (`pack_scrub.cc:127`) and `PagedSegmentRead` (`paged_segment_read.cc:212`, collection proofs) hash packs uncounted. Not in A2's window (scrub_rate 0; no collection proof there, else the counted graph walk would break equality). | the files named | Count them as `Pack` (checked), or narrow the comment and docs to "build and validation hashing". |
| F3 | no | gap | `relay-delta` (the relay's warm trusted recovery) is outside ctest because of the pre-existing shutdown hang, and `hash_once_report.sh` masks its exit (`|| true`); its printed line shows hashed = built, but no failing test guards it. | `tests/detent_delivery/CMakeLists.txt:107`, `hash_once_report.sh:15-17` | Register `relay-delta` when `relay-delta-shutdown-hang` lands. |
| F4 | no | gap | `test_managed_recovery_failure` moved from 80 KiB to 1 KiB, so failure now happens at `.live`'s admit, before any `take_segment`; no test now fails the trusted open after a reserved mapping has moved into the target registry. | `tests/detent_local/test_managed_recovery_failure.cc:153,208-210` | Optional: inject a failure after the first `take_segment` (testing hook) and check the fence and the ledgers. |
| F5 | no | nit | `test_state_equivalence`'s second corruption case flips a pack's last byte and expects the candidate validator to refuse; with `own_build` the pack checksum is no longer compared, so something structural refuses it now. The test still passes but no longer shows what its comment implies. | `tests/detent_local/test_state_equivalence.cc:293-321` | Retarget the case at a check that still runs, or drop it. |
| F6 | no | nit | CHANGELOG: "hashes 698,201 bytes ... instead of 1,396,402" reads as a 2x total; 1,396,402 is the two removed passes, and main's own-build total was three times 698,201. | `CHANGELOG.md:16` | Say "re-hashes 0 bytes instead of 1,396,402" or give both totals. |

## Not run

- ASan, UBSan, TSan and the Linux and macOS matrix: CI's (D54); CI at 96854bda was still running.
- A crash-injection model of the publication: the cold-start path is unchanged from S3 (reviewed there), and
  the new code runs only in process after a committed publication.
- The no-admission, warm-prior, `trusted_start` combination (`TrustedOwn{nullptr, prior}`): reachable only
  if the coordinator recovers a newer selection without a pending plan; I found no production caller and did
  not probe it.
