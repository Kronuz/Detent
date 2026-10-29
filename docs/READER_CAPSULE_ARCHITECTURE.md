# Prepared-reader metadata capsule

**Author:** [Germán Méndez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-10-29
**Status:** Reviewed proposal, not implemented. Format and refusal changes require an owner decision.

| Review | State |
| --- | --- |
| Architecture and compatibility | Conditional recommendation, with the rollout and rollback conditions below |
| Implementation and performance | Pending; resource adoption remains blocked |

## Abstract

Use one combined generation `.read` record for prepared SDK attachment, reusing the existing storage, root and reader-metadata codecs. This reduces metadata file opens from five to three while preserving directory trust, the selected descriptor, the actual shared lease and lazy segment validation.

**Estimate only:** about 60 microseconds of instrumented storage/root record work could disappear before larger reads, framing and checksum costs. No net saving is measured. The tradeoff is explicit: older prepared SDKs reject capsule generations, and new SDKs no longer inspect the standalone storage/root records during attachment. Approval authorizes one prototype, not adoption.

## Motivation and alternatives

The [80-process attribution](https://github.com/Kronuz/Detent/blob/52246e2/docs/measurements/reader-metadata-20261029/README.md) reads exactly 880 metadata bytes per attachment. Observed directory trust takes about 212–222 microseconds of caller CPU; the five opens total about 69–70 microseconds. Instrumentation materially affects these timings.

The [inline ancestor-owner experiment](https://github.com/Kronuz/Detent/blob/1a7faae/docs/measurements/reader-ancestor-20261029/README.md) removes twelve allocations but fails its end-to-end criterion and is discarded. Re-reading the original records retains the work this proposal targets. A new sidecar suffix is unsuitable: old name parsing ignores it, so old collection can delete recognized generation files while leaving the sidecar orphaned. Extending the recognized `.read` file instead makes old readers and collection refuse the unsupported length.

## Mechanism

The capsule is 624 bytes:

| Field | Bytes |
| --- | ---: |
| `DTCAPS01`, version 1, exact length | 16 |
| Existing storage encoding | 64 |
| Existing root encoding | 320 |
| Existing reader-metadata encoding | 192 |
| SHA-256 of the preceding bytes | 32 |

Use the existing integer helpers and nested decoders. There is one implementation of each field codec; the new format adds framing. Keep standalone originals for full daemon recovery and rollback.

Attachment retains trusted ancestry traversal, `selected`, and acquisition and validation of the shared lease. Inspect `.read` with bounded length dispatch. A 192-byte record follows existing validation; a 624-byte record validates the capsule and nested bindings. Reject unsupported lengths, versions, corruption and unsafe ownership without fallback. Missing legacy metadata still validates originals before returning unavailability, preserving refusal precedence.

Both paths enforce storage/generation/checksum identity, root cursor/profile equality, count and admission limits, descriptor ownership and lifetime. Share binding validation and view construction between the paths.

## Changed refusal behavior

With a valid capsule, new SDKs can attach despite missing, malformed, symlinked, wrongly owned, wrongly permissioned or replaced standalone `storage.meta` or generation `.image`. Their ACLs and identities are not inspected. Unsafe or corrupt capsules, descriptors, leases, ancestry and accessed segments still cause refusal.

Full daemon recovery continues validating the originals and can refuse state the capsule SDK serves. Corruption confined to an unused original can therefore remain invisible to an application until recovery or other validation. This asymmetry requires explicit acceptance.

## Publication and collection

Publish the capsule in the existing metadata position before final descriptor publication. Preserve preparation, atomic selection, directory barriers and uncertain-selection handling. Account for 432 additional bytes per generation in actual and peak usage; enlarge bounded record buffers where required. Readiness compares capsule contents with fully recovered originals and counts.

Extend the existing `.read` collection phase and partial-prefix validation for both encodings, including every truncation boundary. Preserve exclusive leases, held/named identity rechecks, payload-before-lease deletion and accounting barriers. Corrupt or ambiguous artifacts remain unreclaimed. There is no second collector.

## Rollout and rollback

| Consumer | Legacy directory | Capsule generations |
| --- | --- | --- |
| Old prepared SDK | Unchanged | Refuses record length |
| New SDK | Existing validation | Capsule validation |
| New daemon | Full recovery; legacy writer mode available | Full recovery and extended collection |
| Old daemon | Unchanged | Unsupported: readiness refuses and collection retains generations |

Deploy compatible SDKs and the new daemon in legacy-writer mode before enabling capsules. Existing 5M state upgrades through full recovery and the existing `PagedPlanBuilder::Republish` path: new generation identity, unchanged logical cursor/state, reused segments and atomic selection. Never rewrite a published record in place.

Rollback requires the new daemon to republish selected state with legacy metadata, then reclaim every unselected capsule generation after readers release their leases. Verify that no capsule generations remain before starting the old daemon. Pinned views or malformed artifacts block rollback; do not delete through them. Crash recovery resumes through the existing recovery and collection paths.

## Limits and acceptance

This changes no IPC or publisher authentication mechanism. Attachment reads rise from 880 to 928 bytes. Directory trust is unchanged, and there is no sub-millisecond guarantee from the estimate. Old daemons cannot safely roll back merely because their collection conservatively retains unsupported generations.

After approval, run one counterbalanced private/canonical × current/capsule prototype on the unchanged twenty-key 5M corpus. Require identical values and end-to-end wall/CPU benefit in both resource arms; otherwise discard. A passing screen must then qualify format fixtures, decoder fuzzing, privileged trust, crash/partial publication, retained-view collection, rollback and supported-platform sanitizers. Uninstrumented resident-data acceptance remains 200/200 first reads below 1 ms in each resource arm, followed by component review.

1. Decide whether the coordinated rollout and changed refusal behavior are acceptable for the prototype.
2. Preserve the existing format if either contract must remain unchanged; continue independent release work.
