> Status: CURRENT (2026-11-02) Ledger of workstream W2 HTTP coroutines (PLAN.md M1).

# http-coroutines

- Design: docs/design/http-coroutines.md (DRAFT); reviews: none yet (architect preliminary review requested)
- Branch / worktree: `design/http-coroutines` at `~/code/Kronuz/Detent-wt/http-coroutines`; head: see `git log -1`
- State: designing (ready for preliminary review)
- Done criteria (set by the lead; contributors tick, never edit):
  - [ ] PLAN M1 done criterion 3 (no `workers`, `should_offload`, `DeferredReply`, `user_data`, `BodyReader` or blocking wait on a reactor thread; every handler a coroutine; body cap from Detent's limits)
- Done: design draft D1-D12, T1-T10, Q1-Q4, S1-S6 (this commit)
- Doing: nothing; waiting for the architect's preliminary review
- Next: fold in review findings; then S1 (`Outcome<T>`), which no question blocks
- Blocked on: Q1 commit placement (owner) for S3; Q2 `--workers` (owner) for S5; Q3 refusal shape (owner) for S4; Q4 http removals (lead, with `xapiand-74`)
- Findings folded in: none
- Last verified: design reads only; no build or test run (none needed for the draft)

## Log
- 2026-11-02: Worktree created from `Detent:aa00b0e`. Read http `679c0f8`, reactor `ade618d`, http-log `431b878`, cluster `428d57d` and UPGRADING-2026-10 (`cpp-libraries:5eedcc7`). Found `Request::user_data` and `defer()` still present at http head (brief premise corrected in the design). Drafted the design.
