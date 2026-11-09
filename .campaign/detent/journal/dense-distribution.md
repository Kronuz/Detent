# dense-distribution: journal

State: waiting on jobs 261108-215111-57b64f 261108-215112-5db839 261108-215113-2c03ee 261108-215113-376435
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Writing `docs/design/dense-distribution-model.py`: imports the dense model pinned at
`Detent:28d3ed9` (job ref `DENSE`), measures the full copy and the deltas in three wire forms
against modeled records calibrated to the measured 5M record copy, and replays the receiver.

## Next

1. Read the four jobs' logs (check 100K `261108-215111-57b64f`, full 5M `261108-215112-5db839`,
   deltas 5M `261108-215113-2c03ee`, crossover sweep 5M `261108-215113-376435`).
2. Write `docs/design/dense-distribution.md` from them (skeleton committed).
3. Fill the Evidence table; State "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A6 | pending: jobs queued at `Detent:4fa06bd` (the model). Probe `261108-214633-1b1530` (unversioned, 100K, not evidence) passed. |

## Result

Pending.

## Rulings I made

1. The dense model is the one at `Detent:28d3ed9` (unchanged in `docs/design/` through
   `Detent:be49a1d`), loaded by path from the job's `DENSE` ref, not copied into this file set.
2. Lead message (2026-11-08, quest Decision 4 at `Detent:a08818f`): keys stay generic. The wire
   names pages by digest and trie paths as length-framed bytes; nothing splits on NUL. "Pages only"
   is presented as its own option with its byte cost.
