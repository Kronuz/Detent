# dense-pages review: security

Commit: Detent:b7b37c699dedf4ed51e3d20c20f41371fddcb9c2
Verdict: CHANGES
Date: 2026-11-06

No production defect found in this focused review. The remaining blockers are the explicit A4 fuzz evidence gates.

## Evidence I relied on

1. `261106-112740-2f388f`: separate ASan+UBSan build, 10/10 selected tests pass at Detent:88dc7fc. This includes dense reads, page/graph validators, refetch, deltas and collection. The production tree is unchanged between that commit and the reviewed commit; the intervening changes affect two test fixtures.
2. `261106-112739-5f92a5`: separate macOS TSan build, 4/4 reader, bitmap, collection and refetch tests pass at Detent:88dc7fc. I read the terminal job record and log.
3. `261106-111645-b25734`: reader/bitmap/collection checks pass, including five additional collection runs. I inspected the malformed-read and warm-summary test cases in the resulting source.
4. `261106-070134-ecbe03`: both refusal and absence-barrier mutants fail, then the restored real-v2 refetch fixture passes. Its source covers process crash cuts, conflicting ownership, retained leases and foreign files.

## What I checked myself

I traced the codec, incremental canonical replay, cold graph walk, warm contextual reuse, reader visits, exact-start bitmap, ownership decode, refusal, census and removal paths against main. Persistent bytes are untrusted; checksums alone do not establish safe framing.

The codec bounds lengths and table arithmetic before slicing. Header key totals are bounded by 32-bit record/namespace limits; page-local metrics are bounded by the 64-KiB page and 256 children. Canonical replay charges repeated scans to its budget. Fixed 203-frame graph stacks, consumed key bytes and forward inline slots bound traversal. Retained buffers and metadata use admitted allocators.

First touch and bitmap hits compare reached length, keys, height and digest. Warm subtree skipping requires the exact reference at the corresponding base path, with matching generation, directory and storage identity. Version-2 ownership is confined to refusal/cleanup; version-3 old-looking damage does not authorize selection removal. Removal follows authenticated recovery and census, with quota credit after the absence barrier. Collection requires exclusive leases.

The remaining framing-valid content mutation exposure is explicitly documented in `docs/design/dense-pages.md:380` and `docs/mechanisms/verified-page-bitmap.md:24`. The review assumes the mapped file extent remains present; a hostile trusted publisher changing storage after verification is not given a new integrity guarantee.

No additional executable probe was necessary to establish the two evidence gaps below; no scratch probes or machine jobs were created by this review.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| SEC-1 | Yes | gap | A4 requires a bounded warm-validator fuzz harness; the randomized harness never constructs `WarmGraphValidator`. Fixed warm summary mutations pass but do not fulfill that requirement. | `tests/detent_local/test_dense_reads.cc:123-187`; codec-only harness at `tests/detent_local/paged_codec_fuzz.cc:23-40`. | Add checksum-correct randomized warm validation against a retained valid base, with live progress/budget checks; run it under ASan+UBSan as a commit-pinned job. |
| SEC-2 | Yes | gap | The required one CPU-hour of decoder fuzzing has no completed qualifying job evidence at review time. | `261106-112828-869301` is still running; its configured 930-second wall limit alone cannot establish the gate. | Record completed sanitizer fuzz jobs with measured CPU time totaling the required hour, covering the required bounded harnesses; retain/replay their corpus. |

## Not run

I did not rerun tests, fuzzers, benchmarks or mutants. I did not independently execute the typed-delta oracle, quantify performance, check Linux/FreeBSD, or review unrelated library changes. The native whole-suite failures and their fixture repairs remain with the game master; this verdict does not declare the complete acceptance matrix green.
