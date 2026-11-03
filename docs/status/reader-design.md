> Status: CURRENT (2026-11-03) Ledger for the reader design workstream.

# reader-design

- Design: docs/design/reader.md on `design/reader`; reviews: none yet.
- Branch / worktree: `design/reader` at `~/code/Kronuz/Detent-wt/reader-design`; base `Detent:d4abd4a`.
- State: designing (ready for preliminary review)
- Done criteria (set by the lead; contributors tick, never edit):
  - none set yet
- Done: the full draft of docs/design/reader.md (D1-D9, T1-T6, alternatives, limits, Q1-Q5, S1-S9), rulings R1-R4 kept verbatim (`Detent:a037429`).
- Doing: nothing; stopped at preliminary review.
- Next:
  1. Architect preliminary review of docs/design/reader.md; fold findings in by ID.
  2. Owner answers to Q1-Q5.
  3. After approval, S1-S4 can start in parallel (no open question blocks them).
- Blocked on:
  - S5 (change word) on Q4, owner; its file belongs to namespace delivery's store format.
  - S7 (CLI) on Q5, owner (reader capacity settings).
  - S8 and S9 (Python, Rust) on Q2, owner (wrapper behavior on wrong type and invalid name).
  - D2's assumptions A1-A4 wait on the namespace-delivery design, which is a brief.
- Findings folded in: none.
- Design vs code: not applicable (nothing built).
- Last verified: every file:line in the design re-read at `Detent:d4abd4a` on 2026-11-03; nothing built, run or measured.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, namespace-delivery brief, legacy inventory (`explore/legacy-inventory` at `Detent:5abdf13`), the reader code, spec, bindings and the first-read evidence. Drafted the design; state set to ready for preliminary review.
- 2026-11-03: Draft committed and pushed as `Detent:a037429` on `design/reader`.
