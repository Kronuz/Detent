> Status: DRAFT (2026-11-04) Ledger for the end-to-end guide. Workstream: how-it-works (tech writer).

# how-it-works

- Branch / worktree: `docs/how-it-works` from `Detent:5475baf`, at `.claude/worktrees/agent-a0182892652fc7d4c`.
- Done criteria (the lead's brief): `docs/HOW_IT_WORKS.md` and a website page that mirrors it, covering actors, the store, the trie, reading, updating, seeding and restart, in that order; D2 sources and PNGs in `diagrams/` beside `05_update_path` and `08_detent_deps`, each checked on light and dark; every number from a cited doc or the code; what is designed but not built marked as such; the website build job green.
- State: guide, website page and diagrams written; the site builds locally (63 pages); CI pending.

## Done

1. `Detent:62ec077` Diagrams `diagrams/10_how_actors` to `16_how_restart` (D2 and PNG), each viewed on white and on `#0d1117`.
2. `docs/HOW_IT_WORKS.md`, `website/src/content/docs/architecture/how-it-works.md` and its sidebar entry (first under Architecture).

## Evidence

- Local site build (`npm ci`, `npm run build` under `quiet light`): 63 pages, the new page renders its seven D2 fences; checked in a headless Chrome screenshot of `astro preview`.
- The site's D2 renderer picks text color from each node's fill, so custom fills rendered light text on light boxes; the website copies of the diagrams drop the custom fills (the repository PNGs keep them).
- Diagram sizes at 1x: 10 1094x540, 11 1073x404, 12 1251x385, 13 987x189, 14 756x462, 15 1111x444, 16 827x234. 12 is wider than the house target; it stays legible at page width, and every narrower layout tried crossed edges or doubled the height.

## Next

1. Push; dispatch `pages.yml` on the branch for its build job. Its deploy job is limited to `main` by the `github-pages` environment's branch policy, so it is expected to be refused there.

## Blocked on

Nothing blocks the guide. Docs that disagree with `main`, for the lead:

1. `website/src/content/docs/architecture/agent.md` ("Agent and Shared Memory") and `docs/ACTORS.md` describe the shared-memory agent that step G removes; G's branch rewrites them.
2. `docs/design/namespace-delivery.md` D10 says a restart is O(namespaces) and opens packs on first use; the code maps every pack at restart, O(segments) (recorded in `docs/status/namespace-s3.md`, "Design vs code").
