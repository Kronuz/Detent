# dense-distribution: journal

State: working
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Writing `docs/design/dense-distribution-model.py`: imports the dense model pinned at
`Detent:28d3ed9` (job ref `DENSE`), measures the full copy and the deltas in three wire forms
against modeled records calibrated to the measured 5M record copy, and replays the receiver.

## Next

1. Probe the model on the 100K corpus (unversioned light job), then commit it.
2. Submit the 5M `full` and `delta` jobs at the branch head with `--ref DENSE=28d3ed9`.
3. Write `docs/design/dense-distribution.md` from the job outputs.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A6 | pending |

## Result

Pending.

## Rulings I made

1. The dense model is the one at `Detent:28d3ed9` (unchanged in `docs/design/` through
   `Detent:be49a1d`), loaded by path from the job's `DENSE` ref, not copied into this file set.
2. Lead message (2026-11-08, quest Decision 4 at `Detent:a08818f`): keys stay generic. The wire
   names pages by digest and trie paths as length-framed bytes; nothing splits on NUL. "Pages only"
   is presented as its own option with its byte cost.
