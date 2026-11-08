> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning. Was: (2026-11-04) Ledger for the dense paged-trie format design.

# dense-pages

- Design: docs/design/dense-pages.md; model: docs/design/dense-pages-model.py. Preliminary review: [dense-pages-design.md](../reviews/dense-pages-design.md), APPROVE WITH REQUIRED CHANGES (2026-11-04).
- Branch / worktree: `design/dense-pages` at `.claude/worktrees/agent-a7d1b230a685025e4`; base `Detent:bea5c43`.
- State: designing (required changes DP-1 through DP-6)
- Done criteria: K1 through K15 below are set by the lead. Contributors attach evidence and tick them; they do not weaken or remove them.
- Done:
  1. The model and this ledger (`Detent:b6932c2`). The model's rebuild of today's trie matches the r12 store exactly (7,000,007 pages, 1,479,001,197 page bytes, 1,441 segments and 1,509,994,805 pack bytes in postorder).
  2. The draft: decisions D1 to D10, gate rows T1 to T10, Q1 and Q2, with the second sweep's 5M figures (`Detent:505d0c0`).
  3. The draft checked against the code it changes: D3's root check (`paged_layout.cc:74`), S3b's repair marked open, and namespace-delivery's D8, D9 and D11 named as such (`Detent:8e1399b`).
  4. The definition-page first touch in the figures, D6 and Limit 2 (`Detent:3afb8ba`).
  5. The build plan in place of the first steps: S1 to S5 with files, done criteria, mutants and the numbers to hit, the landing order against reader S6, S3b and namespace S1, S2 and S7, and every count marked projected (this commit).
- Doing: preliminary findings assigned for design correction; no format implementation is approved yet.
- Next:
  1. Close the builds-on contracts DP-1 (occurrence-aware reuse), DP-2 (empty root) and DP-3 (external summary binding); incorporate DP-4 through DP-6.
  2. Execute S1 to S5 on one implementation branch after the foundational contracts settle. Dispatch hosted CI explicitly; branch pushes alone do not trigger it.
  3. The 5M mixed-churn rerun with the off-path assertion (`.scratch/dense/run3.sh`) did not run: it was stopped at wrap-up. S4's T4 run at 5M covers it. The assertion passes in `check`'s synthetic churn.
- Blocked on: nothing. Q1 (the pack checksum pass) is the lead's or the architect's call; Q2 (the full-copy wire format) is the owner's. Neither blocks S1 to S5.
- Findings folded in: none yet; the preliminary review is complete and its required changes remain open.
- Last verified (2026-11-04):
  - Ruff format, ruff check and pyrefly strict are clean on the model.
  - `dense-pages-model.py check .scratch/dense/check2` passes on the final model ("check: ok", exit 0), under the light hold dense-check2.
  - CI does not run for this branch: `.github/workflows/ci.yml:5` ignores `docs/**`.

## Implementation done criteria

1. [ ] K1 Design contracts: DP-1 through DP-6 have explicit resolutions in the design and model, with builds-on findings closed before affected implementation. Keep pack checksums and record full copies.
2. [ ] K2 S1 codec: exact DTPAGE03/DTROOT03/reference fixtures, reserved-byte refusal, empty-state commitment, initially empty/restart/delete-last/add-first tests, maximum bodies and 256-way branches.
3. [ ] K3 S1 packing: synthetic and 100K page identities/counts/bytes match the corrected model, including repeated-content occurrences; sealing-order, boundary and height mutants die.
4. [ ] K4 S1 decoder proof: checksum-correct malformed summaries and external arithmetic are refused; first touch, walk, graph and warm validators have bounded fuzz harnesses with live checks. One CPU-hour of decoder fuzzing has zero findings and the corpus replays in CI.
5. [ ] K5 S2 build: every full-build consumer uses the one format and one digest; 100K and 5M model projections are reproduced or an evidenced discrepancy is resolved before acceptance. Placement-free identity and seal-order offsets pass; complete file identity remains conditional on derived ids.
6. [ ] K6 S2 validation: reference length/keys/height equal reached headers, warm skipped subtrees use proved summaries, canonical replay has checked arithmetic, and false-height/keys/canonicity/order mutants die.
7. [ ] K7 S2 accounting: separate page, pack and metadata hash counters have exact boundaries and tests; full build, validation and relocation report each separately. Admission, cancellation, yields and bounded retained-cluster memory remain exercised.
8. [ ] K8 S3 reading: landed S6 interfaces are preserved; redirected verified-page placement fails; removing the hit comparison fails its test; first touch counts one digest per page. All reader and trusted-restart regressions pass.
9. [ ] K9 S3 mutation containment: carry S3b's selected bounds/exception behavior through every dense walk visit, with progress bounds; mapped node framing, back edges, table arithmetic and interior placements cannot crash or hang. The residual for undetectable content changes remains documented.
10. [ ] K10 S4 deltas: 1,000 seeded rounds of 1 to 500 changes equal full rebuilds in values and canonical page occurrences. Repeated-content fixtures prove single ownership of physical extents and correct off-path sealing; corrected one-update/mixed-churn model commands and raw outputs establish actual write targets.
11. [ ] K11 S4 relocation: partial and complete relocation preserve all values, root commitment, graph counts and live accounting with zero page hashing; off-path, no-op rewrite and relocation mutants die.
12. [ ] K12 S5 refusal/removal: old root and embedded old page formats trigger refusal/refetch, including interrupted copy and restart. Removed logical-digest fields/domain and Overflow kind have no live callers. Mechanism and public docs describe the resulting single format.
13. [ ] K13 Proof: fixes have failing revert checks and claims have dying mutants; separate ASan+UBSan and threaded-path TSan pass; S3b byte-flip regressions pass on DTPAGE03. CI is green on every required executed job at the final merged head.
14. [ ] K14 Landing reviews: independent proof audit, decoder-safety review and architect landing review clear all blockers and required conditions. A new mechanism receives the corresponding design review before it lands.
15. [ ] K15 Quantitative gate: capture the accepted preceding S6/main baseline; before merging, candidate T5 meets reader T1/T2, including 200/200 resident-data first reads below 1 ms for both namespace counts on the named platform. Lead records 100K/5M projections beside actual measurements, retains A/A and interleaved run evidence where comparing cost, and repeats accepted results on main. T8 file identity follows derived ids, with its remaining dependency explicit.

## Log

- 2026-11-04: started on `Detent:bea5c43`. Read the codec, builder, overlay, readers, validators and the designs this touches (reader.md, namespace-delivery.md, the S3 and S6 ledgers, the cold-bootstrap note at `Detent:d995e86`). Model written; runs queued behind the exclusive hold.
- 2026-11-04: first sweep under light holds. Extract of the r12 store: 5,000,004 records, packs zstd-1 529,843,307 B and zstd-3 526,744,244 B (Python 3.15 `compression.zstd`). Today rebuilt exactly (above). Dense, 64-byte externals: 4 KiB gives 162,100 to 162,383 pages and 62.9 to 63.0 B per record by rule; pages per lookup 6/7 (largest), 5/7 (density), 5/6 (height) at median/p99; 16 KiB gives 60.0 to 60.2 B per record and the same 5/6 with four times the first-touch bytes. The height rule is the candidate; it needs the child's height in the content entry (68-byte externals).
- 2026-11-04: second sweep, 68-byte externals, height rule. 4 KiB: 162,367 pages, 315,432,480 page bytes (63.1 B per record), 302 segments of 315,759,038 B, zstd-1 31,671,463 B and zstd-3 30,775,854 B, 5/6 pages per lookup, SHA-256 input at build 622,748,454 B. 2 to 64 KiB swept (D6). Deltas: one key writes 14,811 B against today's 7,936; 10,000 uniform keys 42,812,457 B against 25,504,788; 10,000 clustered 595,962 against 2,916,424. Churn diff of two full builds: 9,800 updates, 100 inserts and 100 deletes write 16,616 pages of 42,821,363 B, 99 pages (220,903 B) off every changed path. Decoder check: 200,000 keys exact through encoded 4 KiB pages. One namespace alone and hashed names give 5/6 pages per lookup at 4 KiB.
- 2026-11-04: third sweep, partial. The definition-page first touch at 4 KiB is 2,360 B at the median and 3,641 B at p99, against today's 270 and 272; it is in D6 and Limit 2. Node tags moved to today's page kinds (branch 1, definition 2, namespace 3), which changes every digest and no count. The mixed-churn rerun was stopped at wrap-up.
- 2026-11-04: wrap-up for hand-off to the implementing lead. The build plan replaces the first steps, and every count is marked projected. The final `check` passes. State: designing (ready for preliminary review).
- 2026-11-04: Preliminary review requires DP-1 through DP-6. Lead sets K1 through K15 without relaxing the draft's proof obligations. Exact model targets remain projections pending contract corrections and C++ reproduction.
