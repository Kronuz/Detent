# Prepared reader component review

**Date:** 2026-10-26. **Scope:** Candidate based on `Detent:2c04f5d`, including the frozen extent-only mapping diff and inherited prepared-reader publication/reclamation changes. **Verdict:** Accepted within this component scope; no concrete P1/P2 blockers remain.

## Findings closed

1. [Prepared attachment](../../src/detent_local/prepared_reader.cc) enforces every caller corpus bound before deriving verifier limits. `ViewLimits::prepared()` explicitly broadens logical bounds without broadening resource admission.
2. Attachment uses the shared mapping account. Pinned generations and separate readers compete for that account; a new generation does not replenish the allowance.
3. [Metadata prefix validation](../../src/detent_local/reader_metadata.cc) checks visible framing, contract/reserved bytes, complete dynamic constraints and available checksum bytes before reclamation grants deletion authority.

## Extent and lifetime assessment

[SegmentMapping](../../src/detent_local/segment_mapping.hh) distinguishes extent-only mappings from ownership-bearing mappings. Ownership access and registry admission reject the former. The shared mapping engine retains actual-length admission, read-only mapping, inode identity and deferred retirement.

[Lazy access](../../src/detent_local/lazy_page_access.cc) checks protected canonical pack names, file metadata, ACLs, held/named identity, aggregate actual lengths and pack count. Reference containment and physical/logical verification precede exposed spans. Publication prepares `.read` before the descriptor; collection removes it before the payload barrier and lease removal.

The inspected collection fixture holds a prepared old view through replacement and collection, accesses its previously unopened retired pack, then proves removal after releasing that view while the replacement remains readable.

## Evidence and limits

This is an independent source/fixture review; no builds or tests were rerun. Supplied evidence includes native component checks and four focused ASan/UBSan/object-size/pointer-overflow/leak-enabled checks. Two broader native fixture failures have separate corrections and passing targeted repetitions; this verdict does not assert a complete rerun.

The reported resident-data comparison is 0.863 ms versus 0.756 ms median, with 198/200 candidate reads below 1 ms and a 1.262 ms maximum. It establishes no universal deadline. Immutable publisher-owned inodes remain a prerequisite. Managed default activation, old-head preparation, standard path aliases and shared attachment contexts remain outside this acceptance; release qualification is separate.
