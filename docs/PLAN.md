> Status: CURRENT (2026-11-05) Owner scope: complete and optimize Dense pages only. Supersedes the broader November handoff campaign.

# Dense pages

Deliver one complete DTPAGE03/DTROOT03 implementation on `phase/dense-pages`, replacing version 2. Keep record full copies and pack checksums. The existing design and contract reviews are the implementation basis.

## Workflow

Design → lead coordinates complementary developers → integrated feature → code, decoder-safety and optimization reviews → developer fixes → architect's merge ruling.

The owner replaces per-step review gates with whole-feature reviews. Reader S6, legacy removal, damaged-pack repair, grouped syncs, general dependency upgrades and release qualification are parked. None is a prerequisite to Dense pages; integrate against current main's APIs.

## Implementation

| Owner | Piece | Branch |
| --- | --- | --- |
| Full-build developer | Codec, resumable packer, full/snapshot/segment builders, hash instrumentation | `phase/dense-pages` |
| Read/validation developer | Dense walks, first-touch/hit checks, cold/warm graph validation, recovery | `phase/dense-pages-reads` |
| Delta developer | Sparse deltas, occurrence reuse, off-path sealing, placement-only relocation | `phase/dense-pages-deltas` |
| Lead | Interface coordination, assembly, remaining integration, measurement and final acceptance | Main and feature assembly |

## Complete means

1. Full builds, reads, updates, deletions, empty stores and relocation use one format and preserve exact values and ownership. Version-2 stores refuse and refetch safely.
2. Integrated native tests, separate sanitizers, malformed-input/fuzz regressions and required hosted CI pass. No intermediate review queue replaces implementation.
3. Actual 100K/5M measurements cover bytes, pages, hashing, build cost, warm/first reads and sparse-write amplification. Compare the model and preceding main; optimize demonstrated hotspots and state the measured tradeoffs.
4. Whole-feature reviewers clear correctness, decoder safety and optimization findings. The architect rules on the completed feature and its measurements before merge.

Use local hosts and hosted CI, honor quiet holds, and keep the Homebrew tap pinned. No AWS. The [lead ledger](status/campaign-lead.md) records current ownership and parked work.
