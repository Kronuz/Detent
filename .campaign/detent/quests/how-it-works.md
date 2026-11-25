# how-it-works: End-to-end how-it-works guide

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status how-it-works`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/how-it-works.md`.

## Outcome

`docs/HOW_IT_WORKS.md` and a mirroring website page explain actors, the store, the trie, reading, updating, seeding and restart, in that order, with seven D2 diagrams.

## Acceptance criteria

- A1 [artifact] `docs/HOW_IT_WORKS.md` and a mirroring website page cover actors, the store, the trie, reading, updating, seeding and restart, in that order: `docs/status/how-it-works.md`
- A2 [artifact] D2 sources and PNGs in `diagrams/` beside `05_update_path` and `08_detent_deps`, each checked on light and dark: `docs/status/how-it-works.md`
- A3 [artifact] Every number from a cited doc or the code: `docs/status/how-it-works.md`
- A4 [artifact] What is designed but not built is marked as such: `docs/status/how-it-works.md`
- A5 [artifact] The website build job is green: `docs/status/how-it-works.md`

## Contracts

None.

## Decisions

1. The website copies of the diagrams drop custom fills; the repository PNGs keep them.

## May land early

None.
