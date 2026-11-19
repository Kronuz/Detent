# driver-witness-removal-sink: journal

State: waiting on jobs 261119-091207-dec2aa 261119-091209-95db09 261119-091211-f71e84 261119-091214-7078ef 261119-113843-456833 261119-091227-c4741a 261119-091230-bf323c 261119-091232-713002 261119-091235-8e7ebd
Quest: .campaign/detent/quests/driver-witness-removal-sink.md (the lead's; never edit it)

## Doing

Fix at consensus:5f1223c (branch pushed). Jobs queued at it; record results, then ready for review.

What changed (consensus:c64b5cd..5f1223c; c64b5cd is main, fast-forwarded):
1. `include/consensus/driver.h`: `witness(g, index)` becomes `witness_range(g, range)`: a range is
   the witness's own when this node is a witness at its last index, or is no member there and was a
   witness at `range.first - 1` (the apply index). A sticky `Group::witnessed` makes every later
   range the witness's too (a witness incarnation never applies again: core.h:673-678, D9).
2. `test/driver_test.cc`: `witness_case` setup (joiner 4: learner at 2, witness at 4, rpc_entries 2,
   data_matched = commit); `witness_promotion` (G2: Sink holds [1..2] only, applied 6),
   `witness_removal` (removal at 7, ranges [7..8] and [9..9] acknowledged, Sink still through 2,
   applied 9), `witness_removal_restart` (witness checkpoints at 6, restart, removal's [7..7] acknowledged).
3. `docs/DRIVER.md`: the Sink notice row and the exception paragraph state the transitions both
   ways (G1: line 190's claim replaced).

## Next

1. Read the jobs; fill Evidence; State ready for review.
2. A correctness reviewer (the lead launches it).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-091207-dec2aa (cases + all driver_test); fails without (main's driver.h 4b0b214): 261119-091209-95db09 (witness_removal), 261119-091211-f71e84 (witness_removal_restart); rule mutants NOFLAG/NOPREV/FRONT/COMMIT all KILLED: 261119-091214-7078ef (`.scratch/detent/driver-witness-removal-sink/mutants.sh` in the home checkout) |
| A2 | suite 261119-113843-456833; TSan 261119-091227-c4741a; ASan 261119-091230-bf323c; seeds 1-5000 261119-091232-713002, 5001-10000 261119-091235-8e7ebd |
| A3 [artifact] | `docs/DRIVER.md`, "The witness transitions, both ways" (consensus:5f1223c) |

Local run before submitting (quiet light, not evidence): driver_test 151 checks, 0 failures.

## Result

A witness's removal no longer sends its ranges to the Sink: the Driver acknowledges them under the
witness rule, so the application, which holds nothing past the promotion, never gets a range after
a gap. DRIVER.md states both transitions. Review notes folded: G1 (DRIVER.md:190), G2
(`witness_promotion` pins the last-index rule; FRONT and COMMIT mutants KILLED).

## Rulings I made

- "Refused by name" is read as: never handed to the Sink, acknowledged under the named witness
  rule (DRIVER.md). Not fencing: removing a witness is routine (D9 replacement window) and the
  reviewer's G1 race reaches it from a correct leader, so a fence would fail every waiter on an
  ordinary event. No source holds application state for a witness incarnation, so "delivered from
  a source that holds it" has no source here.
- The sticky flag is needed: a removed witness can receive ranges wholly past its removal (one
  AppendRequest of several entries, ranges cut at rpc_entries), where neither end names it.
- The apply-index check (`first - 1`) covers a restart from the witness's checkpoint between
  promotion and removal. A restart from a checkpoint taken after the removal finds no witness at
  either index; its ranges go to the Sink, contiguous with the restored checkpoint (DRIVER.md
  says so). Not a gap: the invariant holds.
- G3 (straddling range acknowledged whole) is left as is: the last-index rule stays, as
  driver-apply-gap ruled and G2 now pins.
- Mutant jobs run `--fresh`: they rewrite driver.h in their checkout.
