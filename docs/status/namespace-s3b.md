> Status: CURRENT (2026-11-04) Ledger for namespace step S3b: repair of a damaged pack (D10.3 on today's store), built ahead of S7 by the owner's ruling (option b).

# namespace-s3b

- Design: `docs/design/namespace-delivery.md` D10.3; the architect's A7 (`docs/reviews/namespace-s3-architect-2026-11-04.md`, `Detent:2b0b9cb`) says what (b) needs; the security review's SR3 and SR4 (`docs/reviews/namespace-s3-security-2026-11-04.md`, `Detent:5f2385b`). Mechanism note: `docs/mechanisms/trusted-restart.md` ("Repair of a damaged pack").
- Branch / worktree: `namespace/s3b` at `.claude/worktrees/agent-a70bf4a502ea488db`; base `Detent:5475baf` (`github/main`, namespace S3 landed).
- State: in progress.
- Owner ruling (2026-11-04): option (b), build the repair now, ahead of S7.
- Done criteria (the lead's, verbatim):
  - A test where T5's damage (100 flips over the 100K store) heals with no operator action: the scrub reports, a repair publishes, every read is correct afterwards, the damaged packs are collected, and the scrub runs clean.
  - A delta that crosses a damaged page heals too; it no longer fails every round.
  - An unchanged-content repair publishes.
  - Other namespaces keep serving throughout.
  - A mutant per claim fails its test, a revert check per fix, and CI green on every job, sanitizers included.
  - The ledger is docs/status/namespace-s3b.md, with the criteria verbatim and evidence for each.
  - A how-it-works update.
- Scope (the lead's, from A7): (1) trigger: a scrub report or a first-touch failure in the update path or the recovery after a publication sets `force_snapshot` and requests a Replace; (2) a repair intent that skips the maintenance equality check, publishing a fresh generation even when the content is unchanged; (3) recovery after the repair publication without the damaged prior; (4) the collector removes the damaged packs once nothing reaches them; (5) SR3: the scrub hashes the file (pread, `F_NOCACHE`/`posix_fadvise`), not the private mapping; (6) SR4: a scrub mismatch clears the pack's verified-page bits; (7) replace the "Under option (a)" text and update `CHANGELOG.md`. Confirm by test that snapshots share no pack with their base.

## Evidence

(filled in as each criterion closes)

## Design vs code

(filled in from the code)

## Log
- 2026-11-04: Branch `namespace/s3b` from `github/main` (`Detent:5475baf`).
