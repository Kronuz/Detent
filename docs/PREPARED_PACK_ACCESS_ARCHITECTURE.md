# Prepared pack access

## Purpose

Reduce fresh-client lookup work without weakening the trusted prepared-reader answer guarantee. The selected descriptor binds the root; authenticated parent references bind each accessed page's segment identifier, extent and physical/logical hashes. The prepared publisher's semantic proof and published-inode immutability remain prerequisites.

The prepared path reads canonical `.pack` files directly. It omits `.owner` records, whose complete-pack checksums the lazy path does not validate. Ownership records remain mandatory for publication, full recovery, registry admission and collection. This change does not alter file bytes or introduce another mapping engine.

## Mapping and admission

The existing `SegmentMapping` supports explicit extent-only construction. It retains the actual mapping length separately from optional ownership evidence and shares file inspection, rounded mapping admission, read-only mapping, inode identity and retirement. Extent-only mappings cannot return `SegmentOwnership` or enter a recovery registry.

For each newly visited pack, the reader checks its canonical path relative to the pinned trusted directory, no symlink, regular-file type, publisher UID, write permissions, ACL, single link and actual length between 80 bytes and 1 MiB. It rechecks named versus held inode identity after mapping. Opened pack count cannot exceed the root's segment count; accumulated actual lengths cannot exceed the publisher-bound physical total. Every returned page still passes reference containment and physical/logical verification. Shared mapping admission bounds coexistence across generations and clients.

A byte-identical referenced page yields identical semantics even when other pack bytes differ. Unused corruption and missing ownership records do not invalidate an accessed page's authenticated answer. Untrusted replacement, accessed corruption and unsafe file metadata remain errors. Hostile root or same-owner mutation is outside the trust boundary; immutable published inodes are required to prevent concurrent truncation of a mapping.

## Qualification

1. Reject ownership access and registry admission for extent-only mappings; retain ownership-bearing behavior.
2. Exercise missing owners, malformed extents, unsafe permissions, hardlinks, symlinks, accessed-byte corruption, shared mapping pressure and inode replacement during acquisition.
3. Hold an old prepared root through compaction and collection, then access a previously unopened pack; release the old root and establish reclamation.
4. Compare fresh processes using the same corpus-derived keys, warmed files and counterbalanced order. Keep instrumentation separate from latency measurements and report outliers, faults and constructor/lookup costs.

Default activation, source-context sharing and old-state preparation remain gates in the [fast-client architecture](FAST_CLIENT_ARCHITECTURE.md).
