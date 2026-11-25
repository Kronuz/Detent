> Status: CURRENT (2026-11-03) code review of `phase/hash-once` (code Detent:a09f76e, head Detent:0be8e30).

# Review: hash-once (certified digests in cold managed recovery)

Verdict: APPROVE WITH CONDITIONS.

No blocker. The certificate is unforgeable from config or disk, is only produced by a no-prior
discovery over every reachable page, and is bound to the registry's own inputs and root. One real
property changed: a pack whose mapped bytes change between discovery and graph validation is no
longer refused (CR1). The doc's argument that this cannot happen is not accurate, so the condition
is to correct the claim and have the owner accept the trade-off.

## What I ran

- ASan+UBSan build (`.scratch/build-asan`, macOS AppleClang, RelWithDebInfo -O1): `test_local_paged_hash_once`
  (every byte, plain and forged owner) and `test_local_paged_snapshot_builder` pass.
- Probe `.scratch/probe_between.cc` (build: `.scratch/build_probe.sh`): discovery over the 880-byte pack, then one
  byte of the mapped segment flipped (mprotect to COW the private page, a stand-in for the file changing under the
  mapping), then `GraphValidator` with and without the certificate.

## Findings

CR1 (non-blocking, owner decision; condition on the docs). A pack changed between discovery and validation is
accepted by the certified validator where main refuses it.
- Probe result, 880 single-byte flips: certified 167 accepted, 713 refused (288 `invalid immutable page`, 90
  `invalid immutable page graph`, 40 `invalid paged definition`, 291 `invalid paged local image`, 4
  `invalid paged segment reference`); uncertified (main's behaviour) 880/880 refused.
- Reach: needs the mapped bytes to differ between the two passes (a same-uid writer on a file that
  `trusted_file`/`named` checks only by owner, mode, size, inode; or page-cache eviction and a re-read of
  bit-rotted media across the 100 s between passes on a 5M store). Not reachable from config. A pwrite to the file
  was not visible through the MAP_PRIVATE mapping on macOS, so I used the mprotect route; Linux was not run.
- The class of exposure already exists after recovery (queries read the mapping without re-hashing), so this removes
  a second look, not the only one. It does change "verified twice, the second moments before use" to "verified once".
- `docs/mechanisms/recovery-digest-once.md` ("The skip is sound because ... read-only shared mappings ... held
  under the generation lease") is wrong on two counts: the mapping is `MAP_PRIVATE` (`segment_mapping.cc:54`), and
  the lease does not stop the file's bytes changing. State instead that soundness assumes pack files are not
  modified during recovery, and name the 167/880 probe result as the cost.
- Options for the owner: (a) accept and document (recommended: the post-recovery read path has the same assumption);
  (b) keep the skip but re-check inode, size and mtime of each pack in the existing post-validation loop
  (`generation.cc`, the `fstatat` loop), which narrows the same-uid case at no hashing cost; (c) drop the skip for the
  segment checksum only (the cheapest digest to repeat is not known; measure before choosing).

CR2 (non-blocking). `SegmentRegistry::certify_digests` is public (`segment_registry.hh:28`) and the optional
`compared` constructor argument is public (`paged_graph_validator.hh:30`), so any code holding a sealed, non-prior
registry can certify it without having hashed anything. Today only `DependencyDiscovery` calls it
(`paged_dependency_discovery.cc:159`) and only `generation.cc:443` passes it. Make `certify_digests` private with
`friend class DependencyDiscovery`. The certificate is never cleared; it lives with the registry (held by the
capability) and is meaningful only for the recovery that made it. Document that, or drop it when recovery
completes.

## Risk answers

1. Reuse across generations / warm vs cold: not reachable. Certification is gated on `!s.inputs->prior`
   (`paged_dependency_discovery.cc:158`); warm discovery and `WarmRegistryMerge` build fresh registries that are never
   certified; `progress` reuses the prior registry but never constructs a `GraphValidator`
   (`generation.cc:437`, `if (!warm)`). `certify_digests` throws if already certified or unsealed, so a recycled
   admission registry fails loudly. The admitted registry must have `physical_bytes()==0`, and every mapping enters
   only through `supply` then Hash. The walk with no frontier validates every page (`paged_dependency_walk.cc:90-112`)
   and requires `pages == root.pages`. Input-set match is pointer and size equality on the registry's own span
   (`paged_graph_validator.cc:32`), so a different set with equal contents or a different registry is refused
   (`std::invalid_argument`, and the new test checks a root mismatch). TOCTOU between the phases: see CR1; that is the
   only gap.
2. Corruption outcomes: `test_paged_hash_once` passes under ASan/UBSan (880 plain flips `invalid paged dependency
   graph`; forged owner 632 `invalid immutable page`, 233 `invalid paged local image`, 15 accepted, all as main).
   The between-phase probe is CR1: it cannot be shown impossible, so it is recorded as the accepted trade-off.
3. Scope and lifetime: see CR2.
4. Mutants and dead code: the four edits in the hash-once `.scratch/mutants.py` are well formed (M1 discovery segment
   hash, M2 walk page digests, M3 graph validator always skips, M4 root compare removed) and the ledger's
   `.scratch/mutants.out` shows each failing a test. I did not rerun them. M3 and M4 are killed only by
   `test_paged_snapshot_builder`, not by `test_paged_hash_once`, which is expected since discovery still rejects. No dead
   verification code: the `Segments` phase and `compare_digests=true` stay live for `CandidatePagedValidator`
   (`candidate_paged_validator.hh:27`), which does not pass a certificate. No test covers the between-phase gap (CR1).

## Not run

Mutants (not rerun), TSan, Linux, the 5M and 100K timings, and the full ctest matrix (CI covers them per the ledger).
Probes: `.scratch/probe_between.cc`, `.scratch/build_probe.sh`, `.scratch/probe_between`.
