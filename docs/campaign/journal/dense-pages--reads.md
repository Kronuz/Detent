# dense-pages--reads: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish prepared-reader/lazy/hash-once fixture migration and composed reader, cold/warm graph, recovery and malformed-input proof.

Adopted `phase/dense-pages-reads` at Detent:7fb6574; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Bring full-build checkpoint into the task through lead-coordinated integration.
2. Complete owned test migration; run committed scoped jobs.
3. Finish decoder and warm-skip mutation/revert proof, then sanitizer jobs.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Bounded dense sources and scoped reader/page/graph fixtures pass historically. Final composed fixtures/sanitizers remain pending.

Historical ledger: `docs/status/dense-reads.md` on `phase/dense-pages-reads`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.
