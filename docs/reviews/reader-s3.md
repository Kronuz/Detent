# Code review: reader S3, verified-page bitmap in LazyPageAccess

> Reviewed 2026-11-03. Branch `phase/reader-s3`, code head `Detent:c5056c1` (CI run 41752136575 green), branch head `Detent:e3d4522` (docs only). Review worktree `Detent-wt/reader-s3-review`, branch `review/reader-s3`, from `Detent:e3d4522`.

## Verdict: APPROVE WITH CONDITIONS

The segment table, the bitmap and the raced-admission fix do what D3 and the two re-check rulings say. I found no way to serve an unverified page, no leaked or unmapped-under-a-reader segment, and no spurious generation failure on x86-64. Two small conditions (CR1 memory order of the admission counters; CR2 a regression test for the neighbour-page case) go in one fix round; neither blocks the design.

## Blocking items

None.

## Conditions (one fix round)

1. CR1: order the two admission counters (below).
2. CR2: add the neighbour-page and unverified-forged-digest cases to the committed tests (below).

## What I ran

Builds under `quiet light`, separate trees, system Apple clang, Debug `-O1 -g`: `build-tsan` (`-fsanitize=thread`) and `build-asan` (`-fsanitize=address,undefined -fno-sanitize-recover=undefined`).

1. `test_local_lazy_page_bitmap` (whole file, 16-thread stress, publish/mark/raced/corrupt/cap windows): TSan 12 of 12 clean, ASan+UBSan 12 of 12 clean. TSan stress runs: 103 and 111 verifications for 55 pages, "both threads raced the publish" and "both threads verified one page" reached each time I looked at the output (runs 1 and 2; logs `.scratch/run/`).
2. `test_local_lazy_page_access`: clean under both.
3. My probes (below), under both sanitizers.
4. A mutant of my own, to show the neighbour probe is not vacuous: granule widened to 160 bytes (`2 * page_header_bytes`). The probe fails at once (`FAIL: target seg offset 96 len 107 at 0 refusals=0`): a page sharing a granule with a verified neighbour is served unchecked. The source was restored (`git status` clean) and the build rebuilt; the probe passes again.

Probes and scripts, all in `.scratch/` of the review worktree: `probe.cc`, `buildprobe.sh` (builds the probe with the bitmap test's flags and link line, `tsan` or `asan`), `run-stress.sh` (N runs of the bitmap test, logs in `.scratch/run/`), `sanitizers.sh` (the builder's script retargeted), `pw/` (probe working directory).

## Risks the lead named

### 1. Publication and reclamation of a slot: sound

- Every read of a slot is `load(acquire)` (`lazy_page_access.cc:~129, find`), every publish is `compare_exchange_strong(release, acquire)` (`publish`), the failed-CAS result is acquire, so `found->identity` is read after the segment's construction on the losing path too. Reclamation `exchange(nullptr, acquire)` (`release_one`) sees fully constructed segments. The pairing is complete on all three paths (lookup, publish loser, teardown).
- Two threads mapping one segment: both map, one CAS wins; the loser takes `destroy(mine)` before it is ever visible, so no reader can hold it; its mapping goes to the retirement domain through the handle. No leak: every exit of `publish` either installs `mine` or destroys it (allocation failure, construct failure, CAS loop exhausted, lost CAS). `publish` window test shows one counted segment and equal addresses.
- A winner whose recount throws (`publish`, after the CAS) leaves the segment installed and counted; the generation is failed. Nothing is unmapped under another reader.
- Teardown: `retire_step` is called by `RetirementNode` only after the last reference is released (`retirement.hh:26-34`); `ReadView` holds the access through `access_owner` (`prepared_reader.cc:173-178`), and the returned spans point into mappings the access owns. So no lookup is in flight when slots are cleared, and `~State` also releases what the budget did not. ASan clean over all stress runs, which destroy 27-segment generations in several budget steps. I could not construct a lookup-during-teardown case through the public types; the guarantee is the handle's, not this code's.
- Slots never go from non-null back to null while lookups can run, so the linear probe needs no tombstones; probes end at the identity or the first null, and the table (power of two at least 2N, load at most one half) cannot fill unless identities beyond `root.segments` are admitted, which the pre-check and recount refuse.

### 2. The bitmap: sound

- Relaxed bit ordering: confirmed safe, and the code does not use it (acquire test, release `fetch_or`). The bit guards bytes of a read-only private mapping of a payload whose name, device and inode were checked after mapping; they are immutable while mapped (publisher rewrites are Limit 5). A thread that sees the bit set reads the same page-cache bytes the verifier read. Nothing the verifier computed is consumed through the bit (not the header, not the definition decode, which are redone by callers). The builder's relaxed mutant surviving under TSan is consistent with this. Can a thread see the bit before the verification is visible? No: release/acquire. And even a reordering could expose nothing but immutable bytes already framed by the extent check.
- A bit for granule g set by verifying a different page: not possible for disjoint pages. Pages of at least 80 bytes that do not overlap start at least 80 bytes apart, so `offset / 80` differs. The disjointness comes from the validators' cell marks (`paged_graph_validator.cc:254-261`), as the re-check says. Probe `neighbour` (all 13 pages of a 5-namespace fixture; 4 byte positions each, 52 cases): corrupt one page, verify every other page first (including those sharing its segment: the namespace and definition pages, and the three branches of the index segment), then 16 threads first-touch the damaged page; all 16 are refused in all 52 cases, TSan and ASan.
- Index arithmetic: words = `(size / 80 + 64) / 64`; the largest granule is `size / 80 - 1` because `offset <= size - 80` after the extent check (`resolve`), so it is inside the bitmap with a word to spare.
- Vector zero-initialisation of `std::atomic<uint64_t>` elements: relied on (`Segment` constructor); holds in C++20 and with the allocator-aware `vector(n, alloc)`. The stress and mark tests would fail otherwise.

### 3. The raced-admission fix (`Detent:043058c`): sound on x86-64, one ordering gap in theory (CR1)

- Pre-check (`admit`) reads `published + 1` and `mapped_bytes`. `published` counts only CAS winners, so it never exceeds the number of distinct published identities; a count refusal therefore means `root.segments` other identities are published, and the re-probe finds the thread's own only if it was published. A thread that was passed by its own segment is rescued by `find`. I found no other spurious path: the recount in `publish` adds only the winner's length, and a loser never counts.
- Mapped bytes over the cap: the cap is not strict. Two new segments can pass the pre-check at once, both map, and the recount of the second refuses it and fails the generation (ledger item 3 says so). The overshoot is bounded by the number of threads in a miss, and by the shared mapping ledger. In a generation that stays within its cap there is no refusal. Probe `cap` (12 namespaces, 13 segments, 16 threads in different orders): 60 iterations on TSan and 100 on ASan, cap exactly met: 0 spurious failures; cap one byte short: 0 missed refusals (the generation always fails); the published count never exceeds the segment count.
- **CR1 (condition, `lazy_page_access.cc` `publish` and `admit`):** `published` and `mapped_bytes` are incremented with relaxed `fetch_add` after a release CAS and read relaxed in `admit` before an acquire slot load. On x86-64 (locked CAS, ordered loads) a reader that sees the new count sees the slot. Under the C++ model, and on ARM, it need not: the counter write is not ordered after the slot write, and the counter read is not ordered before the slot read, so the raced re-probe can still miss the slot and refuse a segment that was published, which is the failure `043058c` fixed. Make the increments `release` and the counter loads in `admit` `acquire`. CI's linux-arm64 job passed, but its stress cannot hit a window this narrow; I did not run ARM.

### 4. Security: corrupt page on first touch: sound

- A bit is set only after `verify_page`, header decode and the kind-specific check succeed, in the same thread, on the same bytes. Failure marks the generation failed and sets nothing. Two threads both verifying the same corrupt page both fail.
- Probe `neighbour` above is the one probe requested: a different, not-yet-verified page in a segment that already has verified pages, corrupted at its first byte, at +40 (logical digest), at +79 (last header byte), and at its last byte (body). 52 of 52 refused by all 16 threads. The builder's `corrupt` test only covers a damaged page whose neighbour sits in another segment; CR2 asks to commit the same-segment case.
- The pre-existing `failed` flag is read relaxed; a thread that has already passed it may verify one more page, which is harmless.

### 5. The changed assertion in `test_lazy_page_access.cc`: consistent with the ruling; one coverage gap (CR2)

- The new first assertion (a forged physical digest at a verified offset returns the same bytes) is exactly the re-check's "dropped digest compare is sound" ruling, and it pins the narrowed behaviour so a later change does not silently restore or widen it. The replacement `wrong` (offset moved by 80, length reduced by 80) is refused and fails the generation as before; it relies on the moved reference landing on a granule no verified page owns, which holds for this fixture (the interior of a definition page) but is a fixture dependency, not a stated one.
- Lost: nothing for the verified case (intended). Not covered anywhere at the lazy level after this change: a forged digest on a never-verified page, which the old assertion also exercised incidentally on a cold lookup. Probe `forged` shows it is refused (`.scratch/probe.cc`, `forged`); commit it.
- **CR2 (condition):** add to `test_lazy_page_bitmap.cc` (a) the `neighbour` case (it is cheap: 52 cases run in well under a second), and (b) `forged` (a flipped physical digest on a never-touched definition page is refused and fails the generation). Both exist in `.scratch/probe.cc` and take their fixture from `prepared_reader_fixture.hh`.

## Other checks

- Existing tests: the builder's Debug `ctest` result (192 of 192) not re-run; the two lazy tests run clean under both sanitizers, which are the ones touching changed code.
- `lru_cache` no longer linked (`src/detent_local/CMakeLists.txt`); `cmake/dependencies.json:231` still declares it (ledger "Blocked on", owner decision).
- Scope: the `LazyPageTesting` hook and `page_verifications()` are test seams, small, and disclosed (ledger items 7). The hook runs one relaxed load per verification as well as per miss (doc says per miss only); immaterial.
- The A/B smoke (8 threads, 36-40 us down to ~0.46 us per call; 1 thread 0.6 down to 0.33 us) was not re-run (benchmarks are not in scope); the shape (a mutex dropped) explains it.

## Non-blocking notes

1. N1: the pack-count and cap checks race by design (item 3 in the ledger); the overshoot is at most one mapped segment per racing thread and is transient. If S6's cap becomes `DETENT_NO_MEMORY` for one lookup (ledger), revisit: the loser of the race should not fail the whole generation then.
2. N2: when S6 shares segment objects (and bits) across generations, a bit means "verified under the root revision of the generation that set it". The ruling says that is monotone under A4; the S6 test should include a namespace page whose watermark exceeds an earlier generation's revision.
3. N3: `verifications` is a shared counter touched on every verification; contended during a cold start only.
4. N4: `verification_cache_bytes` stays unread (ledger option (b) recommended; agree).

## Not run

ARM or any weakly ordered hardware (CR1); a second compiler (CI covers gcc and clang); the full `ctest` and benchmarks; a lookup racing teardown (the retirement handle prevents it, I found no way to build it through the public types); sanitizer builds beyond Debug `-O1`.
