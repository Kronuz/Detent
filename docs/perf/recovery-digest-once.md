> Status: CURRENT (2026-11-03) restart before and after `phase/hash-once`, measured on main's build and the branch's build.

# Restart with each digest compared once

Change: a cold managed recovery compares each segment checksum and page digest once instead of twice
([mechanism](../mechanisms/recovery-digest-once.md)). An `explore/agent-startup` profile found six SHA-256
passes over the same 1.5 GB (9.1 GB, 40.7 s of a 52 s semantic recovery); three remain.

## Method

Host: Intel i9-9980HK, macOS 15.8.1, 32 GiB, no SHA-NI, warm page cache (no `purge`). Release build, ccache.
Arms: `M` main (`d4abd4a`), `N` branch (`a09f76e`), `MM` a second copy of the main binary (the A/A).
Each run clones the store (`cp -cR`), starts `detent-agent` with the upstream port closed, times the start to
"managed local store ready", then stops it. Three rounds at 5M, five at 100K; the arm order rotates each round.
The whole run sat under `quiet exclusive`. Stores: the preserved `managed-5m-r1` and
`capacity-trials/100000-r3/agent-0`, originals untouched. The agent config only differs from the saved one in
the TLS CA path (the saved path is gone) and, at 100K, in relative paths.
Script and raw output: `.scratch/hash-once/` in the main checkout (`ab.py`, `ab-5m.out`, `ab-100k.out`).

## Results (seconds to store ready; lower is better)

| Store | M (main) | MM (A/A) | N (branch) | N vs main |
| --- | --- | --- | --- | --- |
| 5M, 1,441 packs | 121.91, 120.04, 121.36 | 121.34, 121.17, 121.31 | 102.42, 103.39, 103.38 | 18.1 s faster (14.9%) |
| 100K, 40 packs | 3.374, 3.248, 3.249, 3.234, 3.229 | 3.338, 3.306, 3.205, 3.225, 3.224 | 2.886, 2.901, 2.854, 2.851, 2.851 | 0.40 s faster (12%) |

The A/A band is 0.17 s at 5M (121.17 to 121.34) and 0.13 s at 100K (3.205 to 3.338), against an 18 s and 0.40 s
gap. User CPU falls from 48.6-49.4 s to 31.1-31.2 s at 5M (1.06-1.14 to 0.67-0.71 s at 100K); wall falls less
because the startup census (`F_FULLFSYNC` per file, about 55 s at 5M) is unchanged.

## Identity

- All 15 restarts at 5M and 100K exit 0 and print "store ready". After every 5M run, 40 fresh prepared-reader
  probes of `benchmarks/prepared-pack-access-5m-20261025.json` return the expected values: 0 failures in
  all nine runs.
- The preserved `managed-5m-r1` has no prepared metadata, so every restart publishes one new generation
  (same on main). Main's and the branch's published `selected` agree byte for byte after the generation id
  (bytes 32 on: cursor, image length, image SHA-256), as does the original; the new root image is
  byte-identical to the original's and to main's. The `.read` files differ only in the generation-derived bytes.
  The 1,441 packs are reused unchanged. (The "selected unchanged" flag in the raw output is false for all arms,
  main included, for that reason.)

## Not changed

Three comparisons per pack remain: the segment checksum, the page physical digest and the page logical digest.
Each guards something the others do not (see the mechanism note), so going below three passes moves integrity
checking and is the owner's call. The warm path, `GraphValidator` callers without a discovery
(`paged_segment_collection.cc`, `candidate_paged_validator.cc`) and the per-file startup sync are untouched;
the same carrier could serve the first two. 100K runs on one store; the 1M row is not measured.
