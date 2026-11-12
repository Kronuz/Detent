# s3b-followups: journal

State: waiting on jobs 261112-153026-51eb32 261112-153030-e9d783 261112-153034-9bb3cd 261112-153038-a022d0 261112-153041-bddc45
Quest: .campaign/detent/quests/s3b-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fixes and tests committed at 00377bb5. Pass job 261112-152830-3d7cba (exit 0) covers A1-A3. Mutant jobs
(chained, they share a build dir) and the N5 probe are queued: waiting on them.

## Next

1. Read the mutant jobs (all must be killed) and the N5 probe 261112-153041-bddc45; N5 is dropped when its count is negligible (see Rulings).
2. `campaign verify s3b-followups`; fill the Evidence table; State "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |
| A4 | [artifact] `docs/mechanisms/trusted-restart.md`: first Limits bullet (second list) says the scrub withdraws only the agent's own verified bits and a reader process (`PreparedReader`, `LazyPageAccess`) keeps its own until it reopens on the repaired generation. d91078d3 |

## Result

(filled when ready)

## Rulings I made

- N1: EMFILE, ENFILE, ENOMEM on a pack open, fstat or pread throw `std::system_error` from `PackScrub::step` and leave
  the pass at that pack (no report, no repair); the coordinator counts the throw as a scrub failure
  and the next tick resumes there. EINTR is not added (quest names three errnos). The directory open keeps its refusal.
- N3: ENOENT and EIO from a pack open or read during a protected root's walk take the proof-failure path (conservative
  `.live` marks, warning, deferred retry with backoff). Other errors still fail the pass.
- N4: `PackScrub::rebase` keeps packs the running pass hashed clean against the same segment and checksum; a
  damaged pack, a changed record and a finished pass are hashed again. The coordinator calls `rebase` when the pinned root changes.
- Mutant patches live in `tests/detent_testing/mutants/` (inside the file set) and run through the existing
  `tests/detent_delivery/repair_mutant.sh`; the older `s3b-k4-obsolete-report-kept.patch` is unchanged and still applies.
