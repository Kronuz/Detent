# persistent-map component review

1. Date: 2026-10-28.
2. Reviewed: local `persistent-map:e20863c`, `persistent_map.h`, `test/test.cc`, README, CMake and CI definition. Original: `Detent:f1487c2`, `src/detent_authority/persistent_map.hh`; the current original header matches that revision.
3. Checked against: `docs/PERSISTENT_MAP_EXTRACTION_ARCHITECTURE.md`, `docs/reviews/calibration.md`, and the library's ownership and allocation contracts.
4. Checked by: two custom probes compiled with Homebrew Clang 23.1.2, C++20, `-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`, on macOS x86_64 under `quiet light`. Both exit 0. Raw output: `/Users/kronuz/code/Kronuz/persistent-map/.scratch/review/probes.log`. The supplied `/Users/kronuz/code/Detent/.scratch/persistent-map-final-asan-tests.log` records 1/1 committed CTest test passing; this review does not rerun that suite.

## Verdict

**APPROVE WITH CONDITIONS for standalone source publication, conditional on successful hosted CI at the published revision.** No blocking source defect is established. Consumer adoption remains a separate gate; this review qualifies neither production costs nor consumer compatibility.

Paths below are relative to `/Users/kronuz/code/Kronuz/persistent-map` unless qualified.

## Scalability Check

The extraction preserves the AVL rotations and path-copy mechanism (`persistent_map.h:117`, `persistent_map.h:148`). The committed oracle test checks ordered values, historical versions, subtree counts and balance through 5,000 operations (`test/test.cc:49`, `test/test.cc:109`, `test/test.cc:120`). The cursor retains the original checked 128-entry stack (`persistent_map.h:185`).

Retained versions and undrained garbage can grow independently of current `size()`. `retained_nodes()` reports the arena-wide population, including queued nodes (`persistent_map.h:112`, `persistent_map.h:231`). No byte or time bound follows from a node budget; payload cost and version retention remain caller concerns.

## Edge-Case Analysis

1. **Allocation ownership is sound.** Raw node storage is deallocated if construction throws. After successful construction, ownership transfers once into `shared_ptr`; control-allocation failure invokes the deleter and leaves that node queued (`persistent_map.h:102`). Existing insertion failures restore the byte baseline (`test/test.cc:176`). The additional `.scratch/review/erase_failures.cc:8` sweeps leaf, two-child, right-edge and missing-key erasure: 34 injected failures, including 17 control failures. Every failed operation preserves all original values and restores allocated bytes after drainage; final allocator tracking is empty.
2. **Expired-arena cleanup retains the right allocator.** Each deleter stores a rebound allocator (`persistent_map.h:81`). Cross-arena assignment can release the destination's old arena before its old root, so the direct destroy/deallocate branch at `persistent_map.h:92` is necessary. Maps and cursors otherwise release roots before arenas through member destruction order (`persistent_map.h:98`, `persistent_map.h:183`). `.scratch/review/cursor_lifetime.cc:27` verifies retained cursor values across map replacement; its line 54 case exercises queued teardown during cross-arena map/cursor move assignment. Final payload count and both allocator byte counts are zero.
3. **Ordering uses the stored comparator.** Insert, erase, lookup and lower-bound positioning all call the arena instance (`persistent_map.h:140`, `persistent_map.h:152`, `persistent_map.h:210`, `persistent_map.h:238`). `.scratch/review/cursor_lifetime.cc:5` uses a nondefault-constructible comparator and a heterogeneous lookup type that cannot convert to the key. Ascending and descending values remain correct across assignment. Exception preservation is also covered by `test/test.cc:317` and `test/test.cc:354`.
4. **Collection preserves the documented limit.** With an arena owner alive, child releases enqueue work and the loop destroys at most its budget (`persistent_map.h:41`, `persistent_map.h:88`). `.scratch/review/cursor_lifetime.cc:44` verifies zero-budget preservation and 127 successive one-node calls against actual payload destructor counts. Final arena release can destroy descendants synchronously. `README.md:23` and `README.md:25` state both limits accurately and give the collector-retention procedure.

## Dependency & State Audit

The header adds no external dependency or reclamation runtime; CMake exposes one interface target (`CMakeLists.txt:4`). Comparator and allocator state belong to the shared arena; node deleters retain allocation state needed after arena expiration. The allocator resource lifetime and serialized-executor requirements are explicit (`README.md:33`, `README.md:41`). The extraction preserves the original reclamation mechanism rather than introducing a second policy or byte ledger.

## Security & Scale Review

The relevant hostile inputs are allocation failure and caller-supplied keys or payloads. This component has no decoder, network boundary or concurrent API. Balanced paths limit tree depth; expensive comparisons, payload destruction and retained versions remain application-controlled resource costs. Ordinary-pointer and nonthrowing destruction restrictions are checked at `persistent_map.h:74`; the remaining allocator/comparator contracts are documented. Existing allocation-denied lookup, traversal and collection coverage is at `test/test.cc:205`.

## Findings and conditions

1. **No blocking source findings.** The two probes are supplemental evidence. A nonblocking test follow-up is to incorporate erase allocation failures and value-checked cross-arena cursor assignment into `test/test.cc`; its committed allocation sweep currently targets insertion (`test/test.cc:190`), and the retained-cursor lifetime case checks only row count (`test/test.cc:255`).
2. **C1, publication:** Require the hosted supported-platform and sanitizer jobs to reach a successful terminal result for the published revision. `.github/workflows/ci.yml:13` delegates that matrix to the shared workflow; local libc++ sanitizer results do not qualify libstdc++ or FreeBSD behavior. Record the published immutable revision and complete the catalog/provenance bookkeeping required by the blueprint.
3. **C2, adoption:** Preserve the separate gate for exact consumer bytes, failure/lifetime behavior, consumer CI, immutable dependency pins and matched production A/A and A/B costs. This ruling does not satisfy those checks.

## Not run

Hosted CI, Linux/libstdc++, FreeBSD, arm64, TSan, leak sanitizer, benchmarks, fuzzing, mutation testing, consumer tests, checkpoint/export parity, or production cost and default-footprint comparisons. No library source changes, commits, repository creation or publication occur in this review.

## Consumer recheck

**2026-10-29: NO-GO for adoption.** The blueprint's requirement to reject unexplained cost increases remains unsatisfied. This is an unresolved cost finding, not an established source regression. Standalone source approval remains unchanged.

Reviewed candidate `Detent:e060557`, pin `persistent-map:938d65e`, against private baseline `Detent:a5a8304`. The relevant authority/core sources match `Detent:bcfc77e`; the library header is unchanged since the source review. Evidence lives under `.scratch/persistent-map-adoption-20261028/`; `detent-source/docs/measurements/persistent-map-20261028/` holds the initial records.

### Closed checks

The alias preserves the three-parameter source interface (`detent-source/src/detent_authority/persistent_map.hh:6`), the target propagates the library dependency (`src/detent_authority/CMakeLists.txt:3`), and `cmake/dependencies.json:233` pins the full revision. The ledger correctly requires a complete matching C++ rebuild because the underlying namespace changes.

The native log reports 234 passes and one privileged-path skip. `wire-identity.json` matches the checkpoint and both exports. Recomputed raw-record checks confirm 144 tree samples with exactly equal loaded and retained heap in all 16 cases. The lead reports all seven enabled library jobs and all six required consumer jobs successful, in runs 40813864441 and 40814412665 respectively; this recheck does not independently query hosted CI.

### Open finding: unexplained update cost

`tree-cost-long.json` contains 36 samples. For 100K keys, 200-byte keys, batch one and 16 retained roots, 1,024-update CPU medians are 9.523/9.954 ms for identical controls and 10.636 ms after extraction: **+9.22%** against the mean control median. After-extraction samples are 10.636, 9.772 and 11.531 ms. Overlapping ranges show uncertainty, not an explanation.

The apparent improvements elsewhere do not cancel this finding. In the 100K/34-byte case, identical-control medians differ by 41.7%, making an aggregate comparison misleading. The controller starts a separate warmup process (`run_cost_long.py:50`) and then another measured process (`:57`); it cannot control process address layout or allocator history between arms. Its `status: passed` means execution and identity checks passed (`:66`, `:88`), not that costs passed.

The 24 authority samples support the reported 100K costs, including update CPU changes of -0.26%/-3.49%. Equal tree heap and matching executable segment sizes establish useful resource facts. They do not attribute the remaining CPU difference or override the gate.

### One bounded next step

Run one same-process, rotated A/A/B experiment for **only the 100K/200-byte/batch-one/16-root case**, under an exclusive hold. Compile both map types into one executable with matched monomorphic wrappers; A1 and A2 call the identical private-map implementation. Recreate identical starting state outside timing, use the same 1,024-update trace and drainage policy, and rotate all six arm orders twice. Preserve all 36 phase results, value checks and node counts; compare B with both same-block controls and report the A/A spread. This isolates the suspected process/run-state variation without another workload sweep. A stable excess still blocks adoption; inconclusive results also remain NO-GO. Do not optimize source or discard samples to clear this check.

**Not run:** Builds, new measurements, consumer tests or external CI queries. This recheck only reads preserved evidence and appends this ruling.
