# resource-adoption: journal

State: working
Quest: .campaign/detent/quests/resource-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Restart per D162. Worktree `../Detent-wt/detent/resource-adoption` on `quest/detent/resource-adoption`;
state.json records no branch or worktree for this quest (the status is active, so `campaign start`
refuses), so I added the worktree at the path `start` uses. The old `../Detent-wt/resource-adoption`
(on `phase/resource-adoption`, the same commit) is left alone.

Merge of main: Detent:8918a935 keeps the old history and takes main's code.

## Next

1. allocators: `ByteCharge::absorb`, the retirement lost-wakeup fix and interleave seam that main
   carries in Detent's private copy (Detent:10b4b13b, Detent:d5d9bc9b), and a census recount on
   `ReservationBook` for main's `DiskLedger` (settle and observe set `used` to a census).
2. Decision 3 A/B (D162) in allocators.
3. Detent: alias headers, delete `retirement.cc`, `DiskLedger` on `ReservationBook<3>`, pins.

## Blocked on

Nothing yet.

## Evidence

| Criterion | Evidence |
|---|---|

## Result

## Rulings I made

1. Merge strategy: main's code wins every conflict; the 2026-11-02 adoption is redone on main's
   code, not replayed (D162 "restart from current main").
2. A9: main deleted the paged segment collector and `test_paged_segment_collection.cc`
   (Detent:44e8d0c9, "Wire DiskStore through the agent store, delete the old layout"), so variant
   13's test and the fix Detent:febdef6 have no file to apply to. The tests left on main that admit
   `limit - used` hold the ticket only across one expected refusal or one operation that charges
   nothing (`test_paged_disk_store.cc` scenarios 7 and the recovery confirm), never across a retry
   that needs freed headroom, so the race's shape does not recur.
