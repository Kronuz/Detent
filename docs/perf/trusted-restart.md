> Status: CURRENT (2026-11-04) Branch smoke numbers for the agent's cold start before and after trusted restart. Workstream: namespace-s3. Gate rows are measured on `main` by the lead.

# Trusted restart: cold start before and after

Smoke on the branch, not a gate row. The attribution it follows is `docs/perf/agent-startup-2026-11-03.md` on `explore/agent-startup` (`Detent:e8d4e59`).

## Conditions

- Host: the attribution's Intel i9-9980HK macOS laptop, x86-64 binaries (`file` on both), warm page cache, `quiet strict` (exclusive on macOS) for the whole run.
- Before: `Detent:9a5a8c3` (`main` when S3 branched), RelWithDebInfo. After: `namespace/s3` at the commit that adds `tests/detent_testing/restart_timing.cc`, same build type and compiler.
- Harness: `detent_restart_timing publish DIR N` writes a store of N int knobs through the agent's own publication; `restart DIR 1` takes an APFS clone (`cp -cR`), then times `GenerationWriter(clone)` from construction to `StoreStatus::Ready`: the agent's DiskStore cold start (storage census, directory barrier, and `ManagedRecovery::recover`). It is not the whole `detent-agent` process and not the attribution's 5M store, so absolute times are lower than the attribution's (10K: 0.38-0.50 s, 100K: 2.54 s for the process); only the ratio carries over. Each scale is six rounds of base, branch, base again (A/A).
- Raw: `.scratch/timing/results.txt` at measurement time, reproduced below.

## Result

| Scale | Before (median, min-max) | Before again (A/A) | After (median, min-max) |
| --- | ---: | ---: | ---: |
| 10K knobs, 3 packs | 0.179 s (0.125-0.188) | 0.182 s (0.124-0.321) | 0.131 s (0.070-0.185) |
| 100K knobs, 23 packs | 0.670 s (0.620-0.776) | 0.674 s (0.666-1.071) | 0.117 s (0.075-0.121) |

At 100K the cold start is 5.7 times faster by medians, and every after run is below every before run. At 10K the spread is wider than the effect (the A/A range is 0.124-0.321 s and the after range overlaps the before range), so no claim is made at 10K beyond "not slower". What remains at 100K (0.117 s) is the per-file census sync that step S2 replaces; the 5M row of the attribution (118-122 s, 55 s of it per-file `F_FULLFSYNC`) is the lead's to measure.

## Counters (functional, from `test_testing_trusted_restart_oracle`)

At startup the trusted path hashes 0 pack bytes and computes 0 page digests (`RecoveryMetrics::segment_hash_bytes` and `paged::page_digests()`); the full recovery of the same store hashes every pack and walks every page. First touch computes about 238K page digests for a full read of the 100K store and none on a second read.

## Raw

```text
restart base10k run 0: 0.176829 s | branch10k 0.0696276 | A/A base10k 0.186084
restart base10k run 0: 0.126045 s | branch10k 0.182677  | A/A base10k 0.124284
restart base10k run 0: 0.187703 s | branch10k 0.0824029 | A/A base10k 0.12381
restart base10k run 0: 0.181861 s | branch10k 0.134285  | A/A base10k 0.320927
restart base10k run 0: 0.186305 s | branch10k 0.127849  | A/A base10k 0.184207
restart base10k run 0: 0.124699 s | branch10k 0.185458  | A/A base10k 0.180578
restart base100k run 0: 0.621857 s | branch100k 0.0748434 | A/A base100k 0.666013
restart base100k run 0: 0.776302 s | branch100k 0.116987  | A/A base100k 0.673806
restart base100k run 0: 0.717968 s | branch100k 0.120682  | A/A base100k 0.676091
restart base100k run 0: 0.667576 s | branch100k 0.116464  | A/A base100k 0.674449
restart base100k run 0: 0.620474 s | branch100k 0.117657  | A/A base100k 1.07126
restart base100k run 0: 0.672902 s | branch100k 0.117114  | A/A base100k 0.66966
```
