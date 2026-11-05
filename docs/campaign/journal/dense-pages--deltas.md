# dense-pages--deltas: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish composed delta/compaction fixtures and expand independent full-build comparison to the accepted seeded workload.

Adopted `phase/dense-pages-deltas` at Detent:64aab0f; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Compose actual full builders/read validators and run updated delta/compaction fixtures.
2. Exercise 1,000 seeded rounds with 1 to 500 changes and retain exact values/occurrence oracle.
3. Capture actual sparse writes and relocation accounting; prove mutants fail.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Standalone production integration: 50 seeded rounds over 2,000 definitions, path/extent ownership, inline boundaries, no-op zero writes and relocation zero page hashing; historical Debug and ASan/UBSan.

Historical ledger: `docs/status/dense-deltas.md` on `phase/dense-pages-deltas`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.
