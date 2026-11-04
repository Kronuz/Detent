> Status: DRAFT (2026-11-04) Ledger for the end-to-end guide. Workstream: how-it-works (tech writer).

# how-it-works

- Branch / worktree: `docs/how-it-works` from `Detent:5475baf`, at `.claude/worktrees/agent-a0182892652fc7d4c`.
- Done criteria (the lead's brief): `docs/HOW_IT_WORKS.md` and a website page that mirrors it, covering actors, the store, the trie, reading, updating, seeding and restart, in that order; D2 sources and PNGs in `diagrams/` beside `05_update_path` and `08_detent_deps`, each checked on light and dark; every number from a cited doc or the code; what is designed but not built marked as such; the website build job green.
- State: diagrams rendered and checked; guide in progress.

## Done

1. Diagrams `diagrams/10_how_actors` to `16_how_restart` (D2 and PNG).

## Next

1. `docs/HOW_IT_WORKS.md`, then the website page and its sidebar entry; build the site; push; dispatch `pages.yml` (its build job; the deploy job is limited to `main` by the `github-pages` environment's branch policy).

## Blocked on

Nothing yet.
