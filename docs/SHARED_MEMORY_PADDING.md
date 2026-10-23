# Deterministic legacy shared-memory records

**Date:** 2026-10-22. **Status:** Fixed and qualified on the pinned btree.

The container comparison exposed an existing serialization defect: constructing the same snapshot in a different insertion order changed shared-memory byte 12. JSON stayed identical. `KnobRecord` had unnamed alignment padding at bytes 12–15, and `OverrideRecord` had unnamed tail padding at bytes 10–11. Serializing their entire object representations could emit indeterminate padding bytes.

Both spans are now explicit, zero-initialized reserved fields. Record sizes remain 32 and 12 bytes, the integer value remains at offset 16, and all existing field offsets remain unchanged. Existing readers ignore these spans. This changes no meaningful value or reader layout; previously indeterminate bytes become deterministic zeros.

The permanent `test_snapshot_canonical` builds a mixed 1,024-knob snapshot with nested forced and percentage overrides in ascending, descending and seeded shuffled insertion orders. It requires identical JSON and shared-memory bytes. Its optional output-directory argument writes both artifacts for cross-container comparison.

The old btree-backed executable fails the insertion-order check at shared-memory byte 12. After the correction, btree, standard-map and hash-index prototypes pass all three schedules, and their JSON and shared-memory artifacts compare byte-for-byte equal. On the currently pinned btree, the permanent regression, real agent/mapped-reader test and concurrent shared-memory stress test pass 3/3 in 1.23 seconds.
