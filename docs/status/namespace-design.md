> Status: CURRENT (2026-11-03) Ledger for the namespace delivery design workstream.

# namespace-design

- Design: docs/design/namespace-delivery.md on `design/namespace-delivery`; reviews: none yet.
- Branch / worktree: `design/namespace-delivery` at `~/code/Kronuz/Detent-wt/namespace-design`; base `Detent:d4abd4a`.
- State: designing (ready for preliminary review)
- Done criteria (set by the lead; contributors tick, never edit):
  - none set yet
- Done: the full draft of docs/design/namespace-delivery.md (D3-D12, strata fit with proposals P1-P3, reader alignment, T1-T8, W1-W12, alternatives, limits, Q1-Q5, S1-S9), owner rulings D1 and D2 kept verbatim; folds in the lead's three mid-task inputs (the owner's compacted blocks, the strata evaluation and its bar, the Xapiand lead's replication directions).
- Doing: nothing; stopped at preliminary review.
- Next:
  1. Architect preliminary review; fold findings in by ID.
  2. Owner answers to Q1-Q5; P1-P3 agreed or refused by strata's owner and the Xapiand lead.
  3. S1-S4 can start once approved (no open question blocks them).
- Blocked on:
  - S5 on Q5 and Q2 (owner, with the Xapiand lead) and P1 (consensus/journal owners).
  - S6 on Q1 and Q4 (owner) and P2.
  - S9 on Q3 (owner).
  - P3 (compacted blocks) on whether Xapiand wants it; Detent-only otherwise, so out.
- Findings folded in: none.
- Design vs code: not applicable (nothing built).
- Last verified: every file:line re-read on 2026-11-03 at `Detent:d4abd4a`, `completion:576618d`, `consensus:61149a2`, `strata:9d52824`; nothing built, run or measured.

## Log
- 2026-11-03: Worktree created from `github/main` at `Detent:d4abd4a`. Read the brief, the startup attribution (`explore/agent-startup` at `Detent:e8d4e59`), the reader draft (`Detent:a3bb22f`), ACTORS, RELAY, AUTHORITY_EXPORT, DELIVERY_INTEGRATION_ARCHITECTURE, SCALE_TARGETS, STORAGE_BARRIER_ARCHITECTURE, the legacy inventory and the cited code.
- 2026-11-03: First draft used state-plus-tombstones history; replaced by the namespace journal (D6) after the lead relayed the owner's one-replication-mechanism rule and the Xapiand lead's directions. Strata evaluated: replication shape adopted, core and segments not used for delivery or the agent store.
