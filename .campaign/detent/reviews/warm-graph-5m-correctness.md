# warm-graph-5m review: correctness

Commit: Detent:c486f4ed2b65e4a6ed60cf82c51620047f656264 (fix Detent:6a3d64f2)
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261112-143906-a7a2bf (mutant restoring `PagedSemanticLimits{}`): MUTANT KILLED, `invalid warm paged
  graph`, status 134. The mutant changes only `semantics.strings` (overrides stays 256), and
  `semantics.strings` is read once, at `paged_warm_validator.cc:555` (Finalize). So the test fails on
  main at exactly the named `require`. A1/A2 hold.
- 261112-143954-efa99b: Release suite, 263/263 passed at Detent:6a3d64f2 (A3).
- 261112-144057-f1c829: A4 exclusive, arms `full` and `0.5` both publish at 5M (A4).
- CI run 43327886077 at c486f4ed: `test_local_warm_semantic_bounds` passed under linux-asan-ubsan
  in 6.63 s, linux-tsan in 16.00 s, macos-arm64 in 2.22 s and linux-x86_64-clang in 1.61 s. The
  linux gcc x86_64/arm64 and macos-x86_64 jobs also passed: CI at c486f4ed is green. ctest runs `--parallel 2` on these runners.

## What I checked myself

- Risk 1 (paths to publication). Delta (`paged_delta_plan.cc`) and compaction
  (`paged_compaction_plan.cc`) are the only users of `SparsePlanFinalizer`. Every `PagedPlan`
  publishes through `DiskStore::begin`/`begin_reset`, which both call `begin_paged`
  (`disk_store.cc:1662-1666`). The callers are the coordinator (`coordinator.cc:649,652`) and the
  test-only `generation_writer.cc:205`. `PagedPublicationArtifacts` is constructed only at
  `disk_store.cc:1705`, after the admission check at `:1702`. No production path skips admission.
  Recovery re-checks the limits at `generation.cc:394` (progress), `:443` (warm), `:479` (cold) and
  `trusted_restart.cc:57`.
- Probe 261112-152433-b7c191 (`.scratch/review-warm-graph-5m-correctness/admission_strings_mutant.sh`)
  deletes admission's `plan.counts().strings > recovery_limits.strings` clause. It then builds and
  runs every `test_local_*` test: MUTANT SURVIVED, 83/83 pass. See F1.
- Risk 2 (unbounded strings). `semantics.strings` feeds one comparison (`:555`). No allocation is
  sized from it or from `counts.strings`, judged from the grep of `counts.strings` uses in src/.
  The per-leaf additions (`:209,:220`) are bounded by key size (at most 202) plus definition body
  bytes. Leaf count is bounded by `expected.records + namespaces` (`complete()`), and body bytes by
  page bytes, so the uint64 sums cannot wrap. Underflow on subtraction is guarded at `:544-546`.
  The finalizer's validator runs `own_build=true` on the image this process assembled. Hostile wire
  input is bounded earlier by the `DeltaDecoder` limits that the caller passes
  (`response.cc:233`). The overrides bound stays 256; old leaves index `histogram[overrides]`, and
  `validate_definition` bounds that index.
- Risk 3 (siblings). Every production caller of `GraphValidator`/`WarmGraphValidator` passes
  store limits: `generation.cc:443,479`, `candidate_paged_validator.cc:73` and
  `paged_segment_collection.cc:475`. The only remaining default user is a benchmark (N2).
  `CompactionLimits{}` (`coordinator.cc:382`) bounds physical output, not store semantics, so it is
  not this class of bug.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The fix's safety argument is that the store's string limit is enforced at publication admission. No test checks that. Before the fix, a default-configured store (`ViewLimits{}.strings` = 128 MiB) also had the finalizer's 128 MiB check before publishing a delta. Now `disk_store.cc:1702` is the only check before publication, and deleting it passes every local test. A regression there would publish a generation that recovery (`generation.cc:394/443`) then refuses. | probe 261112-152433-b7c191 (MUTANT SURVIVED, 83/83 `test_local_*`) | Extend `test_warm_semantic_bounds`: `begin()` its plan (138,372,024 string bytes) into a `DiskStore` with default `recovery_limits`. Expect `managed paged publication exact base or admission mismatch`. Optionally, with `recovery_limits.strings` raised, it publishes and recovers. Rerun the probe script: it must report MUTANT KILLED. |
| N1 | no | nit | At 5M, A4's 0.5 delta took `plan_s` 512.8 s against 10.6 s for the full copy. The 540 s budget nearly ran out (541 s elapsed). This is outside the quest and matters for T9's verdict. | 261112-144057-f1c829 log | Route to namespace-measure/T9. |
| N2 | no | nit | `benchmarks/paged_snapshot_bench.cc:66` still builds `GraphValidator` with the default 128 MiB semantics. It is a benchmark-only sibling and fails past 128 MiB of strings. | file:line | Pass format or store bounds when the bench is next touched. |
| N3 | no | nit | Every production caller now passes explicit semantics, yet `PagedSemanticLimits`' 128 MiB default member and the `= {}` default parameters (`paged_graph_validator.hh:20,34`, `paged_warm_validator.hh:12,17`) remain. They are the trap that caused this bug. | file:line | Drop the defaults so each caller has to choose a bound. |

## Not run

- The F1 probe ran only `test_local_*`, not detent_delivery or detent_system tests. A grep of
  tests/ found no test that sets `recovery_limits.strings` or expects the admission mismatch.
- No 5M runs and no local sanitizer builds (CI covers both).
- Whether CMakeLists.txt is inside the quest's file set is the lead's decision, as raised in the
  journal.
