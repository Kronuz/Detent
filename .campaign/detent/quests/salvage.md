# salvage: Salvage of the archived scratch work

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status salvage`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/salvage-2026-11-03.md`.

## Outcome

Every archived scratch commit and patch is weighed against `main` and decided; three pieces land with fail-without-fix proofs, and the archive holds nothing unrecorded.

## Acceptance criteria

- A1 [artifact] Every archived item (47 commits in 13 heads, and the tracked patches) has a decision: already on main, superseded, rejected, land, or W2 scope: `docs/status/salvage-2026-11-03.md`
- A2 [artifact] Three pieces land with fail-without-fix proofs: distributor publication atomicity, historical command groups under the retained bound, the bind-collision retry: `docs/status/salvage-2026-11-03.md`
- A3 [artifact] The archive refs and worktree archive carry nothing that is not on `main`, on a pushed track branch, or recorded as dropped: `docs/status/salvage-2026-11-03.md`

## Contracts

None new.

## Decisions

1. The publication-atomicity piece `Detent:0d20dc1` is reverted at `Detent:9cee74a` for W2's owner-thread publication.
2. The retained-state part of `33d36bf` went to the owner, who adopted it (D9, quest `retained-state`).

## May land early

None.
