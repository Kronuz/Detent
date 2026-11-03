> Status: CURRENT (2026-11-03) Ledger for the reader design workstream.

# reader-design

- Design: docs/design/reader.md; reviews: [preliminary](../reviews/reader-design-preliminary.md), APPROVE WITH CONDITIONS.
- Branch / worktree: `design/reader-rev2` at `~/code/Kronuz/Detent-wt/reader-rev`, from `Detent:b55023b` (the first revision landed on main as `Detent:955ab14`, `Detent:64f4076`).
- State: designing (re-check folded in: L2, N1, N2, D3 corrections; the lead confirms)
- Done criteria (set by the lead; contributors tick, never edit):
  - none set yet
- Done:
  - The full draft (D1-D9, T1-T6, alternatives, limits, Q1-Q5, S1-S9), rulings R1-R4 kept verbatim (`Detent:a037429`).
  - Revision folding in the preliminary review: L1 (RD-1) D5 seq_cst publisher and one freer; L2 (RD-3) D10 `pthread_atfork`; L3 (RD-4) D4 rules 1-5 with no older-agent case; L4 (RD-6) lazy attach and directory trust once per store; RD-2, RD-5, RD-7 to RD-11, C1-C4; Q6 added from RD-8. Mapping in the design's "Review findings" table. R1-R4 unchanged (`Detent:7ce4e63`).
- Doing: nothing; stopped for the lead's confirmation.
- Next:
  1. The lead confirms the re-check fold (L2, N1, N2, D3, Limit 5, S6).
  2. Namespace delivery agrees D4's `change` shape (per-namespace sequences, slots, growth) into its D8 (RD-5, C4).
  3. Owner answers Q1-Q6.
  4. S1-S4 may start now (review verdict); S5 and S6 after the re-check.
- Blocked on:
  - S5 (`change` file) on Q4, owner, and on RD-5's agreement with namespace delivery, which owns the file.
  - S7 (CLI) on Q5, owner (reader capacity settings).
  - S8 and S9 (Python, Rust) on Q2, owner (wrapper behavior on wrong type and invalid name).
- Findings folded in: RD-1 to RD-11; L1-L4; C1-C4; re-check L2, N1, N2 and the D3 corrections (C4's RD-5 still needs namespace delivery's agreement).
- Design vs code: not applicable (nothing built).
- Last verified: the review's new citations re-read at `Detent:d4abd4a` on 2026-11-03 (`client.hh:228`, `client.cc:28-34,166,185,223,256,273`, `client_source.cc:82,137-139`, `daemon.cc:39,63`, `lazy_page_access.cc:100,154-167`, `paged_layout.hh:7-8`, `prepared_reader.cc:51`); measured numbers are the review's probes, none mine; nothing built, run or measured.


## Lead calls under "move fast" (2026-11-03; the owner may reverse any)
- Q1 (a), Q3 (a), Q4 (a): the recommendations, matching the review.
- Q2 (a): wrappers return the default and `lookup` gives the status; Python stops raising.
- Q5 (a): one mapped-bytes cap covering the bitmaps; the heap ledger goes.
- Q6 (a): open succeeds on an empty store; lookups return the default with `DETENT_UNAVAILABLE`, retrying open at most once per 200 ms.
- RD-5: namespace delivery adopts D4's `change` shape (per-namespace sequences, slots never reused, grow-only) in its D8.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, namespace-delivery brief, legacy inventory (`explore/legacy-inventory` at `Detent:5abdf13`), the reader code, spec, bindings and the first-read evidence. Drafted the design; state set to ready for preliminary review.
- 2026-11-03: Draft committed and pushed as `Detent:a037429` on `design/reader`.
- 2026-11-03: Preliminary review (APPROVE WITH CONDITIONS) on main at `Detent:55255af`. Worktree `reader-rev` on `design/reader-rev` from it. Read the review, its probe results, namespace-delivery D8 and its review (lead's call: one store directory). Folded in L1-L4, C1-C4 and every RD finding; the review's Q1-Q5 positions change no owner ruling and are recorded with each question; RD-8's open-on-empty-store question added as Q6.
- 2026-11-03: Revision committed and pushed as `Detent:7ce4e63` on `design/reader-rev`.
- 2026-11-03: Re-check on main at `Detent:b55023b` (L1, L3, L4 closed; L2 partial; N1, N2 new). New branch `design/reader-rev2`. Folded in: D10's prepare handler takes each `refreshing` and the child only zeroes counters, T6 fork-midfree; D5 clears freed slots, checks the next slot before attaching, keeps sequences on refusal with a 1-200 ms backoff, states `refreshing` orderings; D4's `change` mapping is a view member; D3 granules from `paged_layout.cc:25-30` and `paged_graph_validator.cc:254-261`, immutable segment identity, cap misses return `DETENT_NO_MEMORY`; Limit 5 states the weakening; S6 passes the directory descriptor (`prepared_reader.cc:96`). Citations re-read at `Detent:d4abd4a`.
