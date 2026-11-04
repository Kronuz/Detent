> Status: CURRENT (2026-11-04) Ledger for the dense paged-trie format design.

# dense-pages

- Design: docs/design/dense-pages.md (drafting); model: docs/design/dense-pages-model.py. No review yet.
- Branch / worktree: `design/dense-pages` at `.claude/worktrees/agent-a7d1b230a685025e4`; base `Detent:bea5c43`.
- State: designing
- Done criteria (set by the lead; contributors tick, never edit): none set yet.
- Done: the model and this ledger (this commit). The model's rebuild of today's trie matches the r12 store exactly (7,000,007 pages, 1,479,001,197 page bytes, 1,441 segments and 1,509,994,805 pack bytes in postorder); the smoke corpus round-trips every key through encoded dense pages.
- Doing: the second sweep (`.scratch/dense/run2.sh`): capacities 2 to 64 KiB under the height rule with 68-byte externals, zstd of the encoded segments, deltas, churn, the decoder check on 200,000 keys, one namespace alone and hashed names.
- Next:
  1. The design: decisions, gate rows, open questions, first steps; then State "designing (ready for preliminary review)".
- Blocked on: nothing.
- Findings folded in: none yet.

## Log

- 2026-11-04: started on `Detent:bea5c43`. Read the codec, builder, overlay, readers, validators and the designs this touches (reader.md, namespace-delivery.md, the S3 and S6 ledgers, the cold-bootstrap note at `Detent:d995e86`). Model written; runs queued behind the exclusive hold.
- 2026-11-04: first sweep under light holds. Extract of the r12 store: 5,000,004 records, packs zstd-1 529,843,307 B and zstd-3 526,744,244 B (Python 3.15 `compression.zstd`). Today rebuilt exactly (above). Dense, 64-byte externals: 4 KiB gives 162,100 to 162,383 pages and 62.9 to 63.0 B per record by rule; pages per lookup 6/7 (largest), 5/7 (density), 5/6 (height) at median/p99; 16 KiB gives 60.0 to 60.2 B per record and the same 5/6 with four times the first-touch bytes. The height rule is the candidate; it needs the child's height in the content entry (68-byte externals).
