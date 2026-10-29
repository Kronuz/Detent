# Prepared-reader capsule prototype

**Date:** 2026-10-29
**State:** Owner-approved prototype; five focused Release tests pass on macOS. Production adoption remains blocked.

The [capsule architecture](../READER_CAPSULE_ARCHITECTURE.md) authorizes a bounded reader prototype. `DTCAPS01` is 624 bytes: a 16-byte frame, the existing 64-byte storage, 320-byte root and 192-byte reader-metadata encodings, and a 32-byte SHA-256 checksum. Nested decoders validate the records and their bindings.

## Scope

1. Prepared attachment accepts legacy 192-byte metadata and 624-byte capsules. Both paths retain selected-descriptor, actual shared-lease, trusted ancestry, cursor/profile, statistics, admission and lazy-segment validation. Missing legacy metadata validates the originals before returning unavailability.
2. A valid capsule is authoritative. Standalone `storage.meta` and generation `.image` may be absent, malformed, symlinked, replaced, or have unsafe ownership, permissions or ACLs without preventing attachment. Their identities and ACLs are not inspected. Full daemon recovery still validates them and can refuse the same directory.
3. The writer, readiness, publication, collection and rollback paths remain legacy. This component cannot publish capsule generations safely in production. The scratch converter applies only to copied benchmark fixtures under an explicit test-only flag.

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

## Next gate

1. Run one counterbalanced private/canonical × legacy/capsule screen on the unchanged twenty-key 5M corpus. Require identical values and paired wall/CPU benefit in both resource arms. No performance claim follows from the format alone.
2. Only after a passing screen, complete the production writer and additional-byte accounting, readiness agreement with recovered originals, partial-publication and collection validation, retained leases, coordinated rollout and rollback.
3. Qualify format fixtures, decoder fuzzing, privileged ownership/ACL cases and supported-platform sanitizers. Uninstrumented resident-data acceptance remains 200/200 first reads below 1 ms in each arm, followed by component review.
