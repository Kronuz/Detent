# dense-pages: Complete and optimize Dense pages

Risk: high. One version-3 format switch, one final landing, complementary task branches.

## Outcome

Full builds, reads, sparse mutations, compaction and relocation use DTPAGE03/DTROOT03 with one placement-free state commitment. Old-format stores refuse and safely refetch. Exact values, physical ownership, bounded decoding and resource accounting hold. Actual 100K/5M measurements establish the layout and its reader/write tradeoffs.

## Acceptance criteria


- A1 Design contracts: DP-1 through DP-6 have explicit resolutions in the design and model, with builds-on findings closed before affected implementation. Keep pack checksums and record full copies.
- A2 S1 codec: exact DTPAGE03/DTROOT03/reference fixtures, reserved-byte refusal, empty-state commitment, initially empty/restart/delete-last/add-first tests, maximum bodies and 256-way branches.
- A3 S1 packing: synthetic and 100K page identities/counts/bytes match the corrected model, including repeated-content occurrences; sealing-order, boundary and height mutants die.
- A4 S1 decoder proof: checksum-correct malformed summaries and external arithmetic are refused; first touch, walk, graph and warm validators have bounded fuzz harnesses with live checks. One CPU-hour of decoder fuzzing has zero findings and the corpus replays in CI.
- A5 S2 build: every full-build consumer uses the one format and one digest; 100K and 5M model projections are reproduced or an evidenced discrepancy is resolved before acceptance. Placement-free identity and seal-order offsets pass; complete file identity remains conditional on derived ids.
- A6 S2 validation: reference length/keys/height equal reached headers, warm skipped subtrees use proved summaries, canonical replay has checked arithmetic, and false-height/keys/canonicity/order mutants die.
- A7 S2 accounting: separate page, pack and metadata hash counters have exact boundaries and tests; full build, validation and relocation report each separately. Admission, cancellation, yields and bounded retained-cluster memory remain exercised.
- A8 S3 reading: landed S6 interfaces are preserved; redirected verified-page placement fails; removing the hit comparison fails its test; first touch counts one digest per page. All reader and trusted-restart regressions pass.
- A9 S3 mutation containment: carry S3b's selected bounds/exception behavior through every dense walk visit, with progress bounds; mapped node framing, back edges, table arithmetic and interior placements cannot crash or hang. The residual for undetectable content changes remains documented.
- A10 S4 deltas: 1,000 seeded rounds of 1 to 500 changes equal full rebuilds in values and canonical page occurrences. Repeated-content fixtures prove single ownership of physical extents and correct off-path sealing; corrected one-update/mixed-churn model commands and raw outputs establish actual write targets.
- A11 S4 relocation: partial and complete relocation preserve all values, root commitment, graph counts and live accounting with zero page hashing; off-path, no-op rewrite and relocation mutants die.
- A12 S5 refusal/removal: old root and embedded old page formats trigger refusal/refetch, including interrupted copy and restart. Removed logical-digest fields/domain and Overflow kind have no live callers. Mechanism and public docs describe the resulting single format.
- A13 Proof: fixes have failing revert checks and claims have dying mutants; separate ASan+UBSan and threaded-path TSan pass; S3b byte-flip regressions pass on DTPAGE03. CI is green on every required executed job at the final merged head.
- A14 Landing reviews: whole-feature correctness/performance review, decoder-safety review and architect landing review clear all blockers and required conditions. A new mechanism receives the corresponding design review before it lands.
- A15 Quantitative gate: capture the accepted preceding S6/main baseline; before merging, candidate T5 meets reader T1/T2, including 200/200 resident-data first reads below 1 ms for both namespace counts on the named platform. Lead records 100K/5M projections beside actual measurements, retains A/A and interleaved run evidence where comparing cost, and repeats accepted results on main. T8 file identity follows derived ids, with its remaining dependency explicit.

## Contracts

The corrected design at `docs/design/dense-pages.md` defines exact root/reference bytes, canonical height packing, empty commitment, summary binding and corresponding-path reuse. Keep record full copies and whole-pack checksums. Generation ownership identifies version 3. No version-2 reader survives.

## Decisions

1. The owner restored the full scope on 2026-11-08 (PLAN.md D4), superseding the Dense-only plan (D1). Dense pages is one quest of the campaign `detent`, and the lead's landing order applies again (D25): `reader-s6` lands before Dense's reading step, since both change `resolve` and `lookup`; `namespace-s3b`'s read-time bounds, exception mapping and byte-flip regressions carry into Dense (A9, A13); `namespace-s1-s2` lands before Dense, which then merges grouped syncs (see `dense-sync-stack`). The baseline is `main`'s APIs when Dense merges it, S6's reader included (A15). Per-step reviews stay replaced by the whole-feature reviews and the architect's merge ruling (Decision 2).
2. The three developers finish complementary pieces, then correctness/performance and decoder-safety paladins review the whole feature. The architect gives the final design/merge ruling. Security is separate because malformed persistent bytes need decoder expertise.
3. A1-A15 preserve the former K1-K15 obligations, with A14's process updated to the owner's whole-feature review instruction. The bounded fuzz/sanitizer/quantitative gates remain; extended soaks and other platforms are deferred.
4. Each live branch is adopted, with component branches represented as tasks inside this quest because none independently delivers a format switch. Original branches, ledgers and reviews remain history.
5. Prior contract approval permits implementation; it does not approve the completed feature. No implementation approval is carried forward.

## History

1. `docs/status/dense-pages.md` on `phase/dense-pages` (full build), `docs/status/dense-reads.md` on `phase/dense-pages-reads`, and `docs/status/dense-deltas.md` on `phase/dense-pages-deltas`.
2. `docs/reviews/dense-pages-design.md`, corrected-contract approval at Detent:7d339fe, reviewing Detent:d379f69. See the carried scoped verdict in `docs/campaign/reviews/dense-pages-contracts.md`.
3. Park: full build Detent:c4fe881, reads Detent:7fb6574, deltas Detent:64aab0f, lead integration Detent:813c3ea. No live holds at adoption.

## May land early

None. The complete format switch waits for integrated acceptance and the architect's ruling.
