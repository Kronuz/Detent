# reader-s5: Reader S5: the change file

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-s5`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/reader-s5-s6.md`.

## Outcome

The agent's store carries a grow-only `change` file (a store word and per-entry sequences) created before any `selected`, bumped at startup and after every `selected` replacement, under the store's exclusive `owner.lock`; readers refuse a `change` the publisher does not own.

## Acceptance criteria

- A1 [artifact] `change` file in the agent: store word, per-entry sequences, grow-only, created with the store before any `selected`: `docs/status/reader-s5-s6.md`
- A2 [artifact] Startup bump after the barrier, before ready: `docs/status/reader-s5-s6.md`
- A3 [artifact] Increments after the rename and barrier of every `selected` replacement: `docs/status/reader-s5-s6.md`
- A4 [artifact] Exclusive `owner.lock` while the agent writes the store: `docs/status/reader-s5-s6.md`
- A5 [artifact] Readers refuse a `change` the publisher does not own (D4 rule 4): `docs/status/reader-s5-s6.md`
- A6 [artifact] Tests with values, a dying mutant per claim, ASan/UBSan and TSan builds, a how-it-works note: `docs/status/reader-s5-s6.md`
- A7 [artifact] CI green on `reader/s5`: `docs/status/reader-s5-s6.md`

## Contracts

New on-disk file `change` (D4); the census counts it, so a fresh store is `{80,3,0}`.

## Decisions

1. Lead rulings: slot 0 only until namespace-delivery S7; the existing completion owner lock is the exclusive lock; a fresh store at `{80,3,0}` is accepted (D26).

## May land early

None.
