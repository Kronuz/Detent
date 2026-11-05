> Status: CURRENT (2026-11-04) Ledger for the dense paged-trie format design.

# dense-pages

- Design: docs/design/dense-pages.md (complete draft, ready for the architect's preliminary review); model: docs/design/dense-pages-model.py. No review yet.
- Branch / worktree: `design/dense-pages` at `.claude/worktrees/agent-a7d1b230a685025e4`; base `Detent:bea5c43`.
- State: designing (ready for preliminary review)
- Done criteria (set by the lead; contributors tick, never edit): none set yet.
- Done:
  1. The model and this ledger (`Detent:b6932c2`). The model's rebuild of today's trie matches the r12 store exactly (7,000,007 pages, 1,479,001,197 page bytes, 1,441 segments and 1,509,994,805 pack bytes in postorder).
  2. The draft: decisions D1 to D10, gate rows T1 to T10, Q1 and Q2, with the second sweep's 5M figures (`Detent:505d0c0`).
  3. The draft checked against the code it changes: D3's root check (`paged_layout.cc:74`), S3b's repair marked open, and namespace-delivery's D8, D9 and D11 named as such (`Detent:8e1399b`).
  4. The definition-page first touch in the figures, D6 and Limit 2 (`Detent:3afb8ba`).
  5. The build plan in place of the first steps: S1 to S5 with files, done criteria, mutants and the numbers to hit, the landing order against reader S6, S3b and namespace S1, S2 and S7, and every count marked projected (this commit).
- Doing: nothing.
- Next:
  1. The architect's preliminary review of the design.
  2. The implementing lead executes the build plan (S1 to S5 on one `phase*/` or `design/` branch).
  3. The 5M mixed-churn rerun with the off-path assertion (`.scratch/dense/run3.sh`) did not run: it was stopped at wrap-up. S4's T4 run at 5M covers it. The assertion passes in `check`'s synthetic churn.
- Blocked on: nothing. Q1 (the pack checksum pass) is the lead's or the architect's call; Q2 (the full-copy wire format) is the owner's. Neither blocks S1 to S5.
- Findings folded in: none yet.
- Last verified (2026-11-04):
  - Ruff format, ruff check and pyrefly strict are clean on the model.
  - `dense-pages-model.py check .scratch/dense/check2` passes on the final model ("check: ok", exit 0), under the light hold dense-check2.
  - CI does not run for this branch: `.github/workflows/ci.yml:5` ignores `docs/**`.

## Log

- 2026-11-04: started on `Detent:bea5c43`. Read the codec, builder, overlay, readers, validators and the designs this touches (reader.md, namespace-delivery.md, the S3 and S6 ledgers, the cold-bootstrap note at `Detent:d995e86`). Model written; runs queued behind the exclusive hold.
- 2026-11-04: first sweep under light holds. Extract of the r12 store: 5,000,004 records, packs zstd-1 529,843,307 B and zstd-3 526,744,244 B (Python 3.15 `compression.zstd`). Today rebuilt exactly (above). Dense, 64-byte externals: 4 KiB gives 162,100 to 162,383 pages and 62.9 to 63.0 B per record by rule; pages per lookup 6/7 (largest), 5/7 (density), 5/6 (height) at median/p99; 16 KiB gives 60.0 to 60.2 B per record and the same 5/6 with four times the first-touch bytes. The height rule is the candidate; it needs the child's height in the content entry (68-byte externals).
- 2026-11-04: second sweep, 68-byte externals, height rule. 4 KiB: 162,367 pages, 315,432,480 page bytes (63.1 B per record), 302 segments of 315,759,038 B, zstd-1 31,671,463 B and zstd-3 30,775,854 B, 5/6 pages per lookup, SHA-256 input at build 622,748,454 B. 2 to 64 KiB swept (D6). Deltas: one key writes 14,811 B against today's 7,936; 10,000 uniform keys 42,812,457 B against 25,504,788; 10,000 clustered 595,962 against 2,916,424. Churn diff of two full builds: 9,800 updates, 100 inserts and 100 deletes write 16,616 pages of 42,821,363 B, 99 pages (220,903 B) off every changed path. Decoder check: 200,000 keys exact through encoded 4 KiB pages. One namespace alone and hashed names give 5/6 pages per lookup at 4 KiB.
- 2026-11-04: third sweep, partial. The definition-page first touch at 4 KiB is 2,360 B at the median and 3,641 B at p99, against today's 270 and 272; it is in D6 and Limit 2. Node tags moved to today's page kinds (branch 1, definition 2, namespace 3), which changes every digest and no count. The mixed-churn rerun was stopped at wrap-up.
- 2026-11-04: wrap-up for hand-off to the implementing lead. The build plan replaces the first steps, and every count is marked projected. The final `check` passes. State: designing (ready for preliminary review).
