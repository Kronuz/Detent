# Prepared-reader capsule prototype

**Date:** 2026-10-29
**State:** Owner-approved component integration; production validation in progress. Adoption remains gated.

The [capsule architecture](../READER_CAPSULE_ARCHITECTURE.md) defines the owner-approved component. `DTCAPS01` is 624 bytes: a 16-byte frame, the existing 64-byte storage, 320-byte root and 192-byte reader-metadata encodings, and a 32-byte SHA-256 checksum. Nested decoders validate the records and their bindings.

## Scope

1. Prepared attachment accepts legacy 192-byte metadata and 624-byte capsules. Both paths retain selected-descriptor, actual shared-lease, trusted ancestry, cursor/profile, statistics, admission and lazy-segment validation. Missing legacy metadata validates the originals before returning unavailability.
2. A valid capsule is authoritative. Standalone `storage.meta` and generation `.image` may be absent, malformed, symlinked, replaced, or have unsafe ownership, permissions or ACLs without preventing attachment. Their identities and ACLs are not inspected. Full daemon recovery still validates them and can refuse the same directory.
3. `DiskStoreOptions::reader_metadata` selects legacy or capsule payloads in the existing publication operation, with legacy as the default. Actual and peak reservation charge the additional 432 bytes. Readiness compares the selected metadata with recovered originals and requests Republish when the configured encoding differs.
4. The existing metadata collection phase handles both encodings and their publication prefixes. Upgrade and rollback use new generation identities with the same logical state. The one-shot `--verify-legacy-metadata` check acquires the same managed owner session and rejects capsule or malformed leftovers.
5. Publication still preserves original files and all preparation, selection and directory barriers. There is no second publisher, collector or locking mechanism. Shared pins and the independent CL-2 ownership upgrade remain unchanged.

## Validation

The isolated Release build uses LLVM 23.1.2, an explicit macOS SDK sysroot, and copied dependency sources. All compilation and tests run under `quiet light` with at most four build jobs.

1. `test_local_reader_capsule`, `test_local_reader_metadata`, `test_local_lazy_page_access`, `test_local_reader` and `test_local_managed_reader` pass. CTest reports 1.99 seconds total for the six selected entries; the capsule test takes 0.66 seconds.
2. `test_local_trusted_paths` skips through its existing privileged harness. Privileged ownership and ACL qualification remains pending.
3. The capsule regression covers all 624 truncations and single-byte corruptions, resealed nested corruption, nested binding, a synthetic 5M-record root codec roundtrip, legacy/capsule value and override equivalence, limits, refusal precedence, unsafe metadata and lease modes, expected cursors, and lazy payload corruption. The synthetic root checks codec bounds only; it is not a 5M graph or performance result.

Reproduce the focused test selection after building the corresponding targets:

```sh
ctest --test-dir BUILD --output-on-failure -j1 \
  -R '^test_local_(reader_capsule|reader_metadata|lazy_page_access|reader|managed_reader|trusted_paths)$'
```

## Component qualification

The reviewed exclusive four-arm twenty-key 5M screen passes. Paired median wall/caller CPU differences are -44.764/-44.785 microseconds for private resources and -27.1945/-27.1065 microseconds for canonical resources; every pair allocates four fewer objects. The lead owns the raw screen and published measurement record. These results do not establish the uninstrumented 200/200 acceptance target.

All 22 selected native component entries pass across the initial run and corrected-fixture rerun. Coverage includes both writer modes, quota/accounting, original-boundary failure and cancellation, every capsule truncation through collection, retained readers, equal-cursor upgrade/rollback, restart, corrupt leftovers, offline verification and actual agent flags.

## Remaining gates

1. Complete native component tests and one consolidated Architect/code/security review.
2. Qualify decoder fuzzing, privileged ownership/ACL cases, supported-platform sanitizers and the canonical resource closure's publisher/verifier exclusion. CL-2 remains an independent release gate.
3. Require uninstrumented resident-data acceptance of 200/200 first reads below 1 ms in each resource arm. Keep legacy writing as the deployment default until the coordinated rollout gates close.

## Native fixture corrections

The first production-profile selection reports 20/22 entries passing in 369.37 seconds. Both failures are new test-fixture errors; production sources remain unchanged for the focused rerun.

1. `test_managed_config` enters the new metadata checks with `compaction_interval_ms` still set to the intentionally invalid value 86,400,001 by the preceding interval-rejection loop. The fixture resets that field to zero before checking metadata modes. The rejected interval remains covered; the production parser does not change.
2. `test_local_reader_capsule_partial_reclaim` initially rethrows every `collection_error`. Its intentionally partial original roots cause the existing segment collector to report `unsafe named paged root` at [paged_named_root.cc](../../src/detent_local/paged_named_root.cc#L298). This is conservative segment retention, independent of generation artifact reclamation. The legacy oracle's [driver](../../tests/detent_local/test_paged_root_partial_reclaim.cc#L13) permits that diagnostic while asserting generation reclamation and exact census accounting. The capsule fixture permits only this exact runtime-error message; other collection errors and every store error still fail. Assertions still require every valid unpinned generation artifact to disappear, every corrupt artifact to remain, the pinned generation to survive until lease release, selected reads to succeed, and disk accounting to equal the real census.

The corrected fixtures and their TLS setup pass in the focused rerun (3/3 entries, 17.55 seconds; the 625-boundary GC case takes 17.07 seconds). Production sources remain unchanged. This evidence is not an all-green first attempt.

The complete component at `Detent:fe0faf0` receives conditional architecture/code/security approval with no blocking implementation defect identified. Platform, sanitizer, privileged-trust, fuzz, canonical-resource and 200/200 latency conditions remain open. Legacy remains the default.
