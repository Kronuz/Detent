# dense-distribution: journal

State: waiting on jobs 261108-215111-57b64f 261108-215112-5db839 261108-215113-2c03ee 261108-215113-376435
Quest: .campaign/detent/quests/dense-distribution.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Model committed (`Detent:4fa06bd`); its four jobs are queued behind host load (still queued after
one 600 s wait, 2026-11-08 22:0x). Design direction, to be written once numbers land: a transfer
lists the pages the receiver lacks in the target's seal order, laid out as the receiver writes
them; full copy = transfer from the empty base (canonical packs); wire form zeroes the header digest
and in-transfer child digests and names in-transfer children by (transfer segment from 1, offset);
base children keep their digest and are resolved by trie path (generic bytes) in the base; the
receiver hashes bottom-up and checks the one root digest; relays keep a page window of recent
generations for Merkle diffs and a canonical copy made by relocation (no digest); smaller of pages
and records per update, "pages only" as an owner option. 100K probe: wire form 594 KB vs records
542 KB measured; replay reproduces the build's files; refusals work.

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
