> Status: CURRENT (2026-11-03)

# hash-once

- Design: none (an explorer's attribution, `docs/perf/agent-startup-2026-11-03.md` on `explore/agent-startup`); mechanism note `docs/mechanisms/recovery-digest-once.md`
- Branch / worktree: `phase/hash-once` at `~/code/Kronuz/Detent-wt/hash-once`; head: see the log
- State: in review (ready for code review)
- Done criteria (set by the lead):
  - [x] K1 each digest of a pack is computed once per cold recovery, every comparison kept: 6 passes to 3 (`hashed=880` for an 880-byte pack, was 1760)
  - [x] K2 identity on the preserved 5M store (`selected`, 40/40 probes), before and after: 0 probe failures in 9 runs; `selected` equal after the generation id (docs/perf/recovery-digest-once.md)
  - [x] K3 corruption tests (all 880 byte flips, plain and forged owner), 4 mutants killed, ASan/UBSan and TSan clean on 8 touched/related tests
  - [x] K4 5M and 100K restart A/B with an A/A under `quiet exclusive`: 5M 121.1 to 103.1 s (A/A 121.2), 100K 3.27 to 2.87 s (A/A 3.26)
- Done: change and tests (`Detent:a09f76e`), perf and mechanism notes
- Doing: nothing
- Next: code review; the same carrier could serve `paged_segment_collection.cc` and `candidate_paged_validator.cc` (separate step)
- Blocked on: nothing
- Findings folded in: CR1 (mechanism note states the unchanged-pack-files assumption, `MAP_PRIVATE`, and the 167/880 probe result), CR2 (`certify_digests` private to `DependencyDiscovery`; `record_reachability` clears the certificate; `compared` argument kept, it is inert without a certificate)
- Design vs code: no design; the lead's brief said "hash each pack once". Three distinct digests exist per pack (segment checksum, page physical, page logical), each with its own comparison, so the floor with every comparison kept is three passes, down from six. Collapsing further changes what is checked (an owner-record-independent chain from `selected`), which belongs to the owner.
- Last verified (2026-11-03): native Release `ctest -R "local|recovery|managed"` 85/85 pass, 1 skipped (trusted paths); mutants M1 (discovery segment hash skipped), M2 (discovery page digests skipped), M3 (graph skips always), M4 (certificate root unchecked) each fail `test_local_paged_hash_once` or `test_local_paged_snapshot_builder`; ASan/UBSan and TSan (macOS AppleClang, no LSan) clean; CI run 41748094042: all jobs green after a rerun of `linux-x86_64-clang`, whose first attempt aborted in `test_local_paged_publication_artifacts` (an expected `StoragePressure` was not thrown; passes 60/60 locally, unrelated to hashing, flake not attributed)

## Log
- 2026-11-03: branch from main d4abd4a; baseline agent and the new test built; the new test on unchanged code gives `hashed=1760 pack=880` (twice), 880 plain flips all `invalid paged dependency graph`, 632 `invalid immutable page` + 233 `invalid paged local image` + 15 accepted (padding) with the owner record forged.
- 2026-11-03: mutants, sanitizers, 5M and 100K A/B/A; numbers in docs/perf/recovery-digest-once.md.
