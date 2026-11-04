> Status: CURRENT (2026-11-04) Ledger for the dense paged-trie format design.

# dense-pages

- Design: docs/design/dense-pages.md (drafted, figures being completed); model: docs/design/dense-pages-model.py. No review yet.
- Branch / worktree: `design/dense-pages` at `.claude/worktrees/agent-a7d1b230a685025e4`; base `Detent:bea5c43`.
- State: designing
- Done criteria (set by the lead; contributors tick, never edit): none set yet.
- Done:
  1. The model and this ledger (`Detent:b6932c2`). The model's rebuild of today's trie matches the r12 store exactly (7,000,007 pages, 1,479,001,197 page bytes, 1,441 segments and 1,509,994,805 pack bytes in postorder).
  2. The design's decisions D1 to D10, gate rows T1 to T10, Q1 and Q2 and steps S1 to S5, with the second sweep's 5M figures; the model gains the definition-page first touch, `churn --out`, the synthetic corpus, a self-check and today's page-kind tags (this commit).
- Doing: the third sweep (`.scratch/dense/run3.sh`), queued behind an exclusive hold: the definition-page first touch per capacity, and the mixed churn rerun with the off-path assertion.
- Next:
  1. Put the first-touch figures in D6 and Limit 2; rerun `check` on the final model; State "designing (ready for preliminary review)".
- Blocked on: nothing.
- Findings folded in: none yet.
- Last verified: ruff format, ruff check and pyrefly strict clean on the model (2026-11-04). `dense-pages-model.py check` passed before the node tags moved to today's page kinds; it reruns after the third sweep.

## Log

- 2026-11-04: started on `Detent:bea5c43`. Read the codec, builder, overlay, readers, validators and the designs this touches (reader.md, namespace-delivery.md, the S3 and S6 ledgers, the cold-bootstrap note at `Detent:d995e86`). Model written; runs queued behind the exclusive hold.
- 2026-11-04: first sweep under light holds. Extract of the r12 store: 5,000,004 records, packs zstd-1 529,843,307 B and zstd-3 526,744,244 B (Python 3.15 `compression.zstd`). Today rebuilt exactly (above). Dense, 64-byte externals: 4 KiB gives 162,100 to 162,383 pages and 62.9 to 63.0 B per record by rule; pages per lookup 6/7 (largest), 5/7 (density), 5/6 (height) at median/p99; 16 KiB gives 60.0 to 60.2 B per record and the same 5/6 with four times the first-touch bytes. The height rule is the candidate; it needs the child's height in the content entry (68-byte externals).
- 2026-11-04: second sweep, 68-byte externals, height rule. 4 KiB: 162,367 pages, 315,432,480 page bytes (63.1 B per record), 302 segments of 315,759,038 B, zstd-1 31,671,463 B and zstd-3 30,775,854 B, 5/6 pages per lookup, SHA-256 input at build 622,748,454 B. 2 to 64 KiB swept (D6). Deltas: one key writes 14,811 B against today's 7,936; 10,000 uniform keys 42,812,457 B against 25,504,788; 10,000 clustered 595,962 against 2,916,424. Churn diff of two full builds: 9,800 updates, 100 inserts and 100 deletes write 16,616 pages of 42,821,363 B, 99 pages (220,903 B) off every changed path. Decoder check: 200,000 keys exact through encoded 4 KiB pages. One namespace alone and hashed names give 5/6 pages per lookup at 4 KiB.
