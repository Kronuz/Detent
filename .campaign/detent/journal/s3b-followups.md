# s3b-followups: journal

State: working
Quest: .campaign/detent/quests/s3b-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fixes committed (d91078d3, mutants d3624b1e). Unit and mutant jobs submitted for A1-A3 (see Evidence). Tuning the
coordinator-level `scrub-carry` test (N4 wiring) from a local run; N5 measured with a probe job.

## Next

1. Read the A1-A3 jobs; submit the `scrub-carry` pass job and its wiring mutant (`s3b-fu-a3-coordinator-begins`).
2. Run the N5 probe (`tests/detent_testing/probes/n5_obsolete_count.sh`) as a job and decide: drop N5 or keep.
3. `campaign verify s3b-followups`; fill the Evidence table; State "ready for review".

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
