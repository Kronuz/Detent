# consensus-late-append-tests: consensus: tests for late-success read probe and the snapshot guard

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-late-append-tests`.

## Outcome

consensus-late-append fixed two paths without tests (reviews/consensus-late-append-correctness.md:70-71 F1, F2): a late success must acknowledge its read probe (R4), and the snapshot guard must hold. After this quest each has a test that fails when its fix is reverted.

## Acceptance criteria

- A1 A late success acknowledges its read probe: a test that fails with the R4 fix reverted (--fails-without): jobs.
- A2 The snapshot guard refuses what it guards: a test that fails with the guard removed (--fails-without): jobs.
- A3 The suite passes: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R4).
